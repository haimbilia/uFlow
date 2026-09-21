import contextlib
import io
from pathlib import Path
import shutil
import struct
import importlib.util
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("payload_sync", Path(__file__).resolve().parents[1] / "tools/sync-native-payload.py")
sync = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sync)


class PayloadSyncTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.project = Path(self.temp.name) / "project"
        self.sources = Path(self.temp.name) / "sources"
        self.payload = self.project / "native-installer/content/payload"
        self.manifest = self.project / "native-installer/source/generated_files.hpp"
        rows = []
        for name, source in sync.inputs(self.project, self.sources).items():
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_bytes(("fixture " + name).encode())
            if name == sync.REGION:
                continue
            dest = self.payload / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, dest)
            rows.append(f'    {{"{name}","{sync.digest(source)}",{source.stat().st_size}ULL,1u,\'r\'}},')
        unmanaged = self.payload / "wiiu/vc-bases/nes/default/meta/meta.xml"
        unmanaged.parent.mkdir(parents=True)
        unmanaged.write_bytes(b"base fixture")
        rows.append(f'    {{"wiiu/vc-bases/nes/default/meta/meta.xml","{sync.digest(unmanaged)}",12ULL,16u,\'b\'}},')
        self.manifest.parent.mkdir(parents=True)
        self.manifest.write_text("return {\n" + "\n".join(rows) + "\n};\n")
        font = self.project / "app/data/glass.ttf"
        font.parent.mkdir(parents=True)
        font.write_bytes(b"font")
        shutil.copyfile(font, self.project / "native-installer/content/font.ttf")
        original = sync.REGION_PATCH_HASH
        self.addCleanup(setattr, sync, "REGION_PATCH_HASH", original)
        sync.REGION_PATCH_HASH = sync.digest(self.sources / "region_free_plugin/regionfree.wps")
        original_backend = sync.PLUGIN_BACKEND_HASH
        self.addCleanup(setattr, sync, "PLUGIN_BACKEND_HASH", original_backend)
        sync.PLUGIN_BACKEND_HASH = sync.digest(self.sources / "WiiUPluginLoaderBackend/PluginBackend.wms")
        original_ftp = sync.FTPIIU_PATCH_HASH
        self.addCleanup(setattr, sync, "FTPIIU_PATCH_HASH", original_ftp)
        sync.FTPIIU_PATCH_HASH = sync.digest(self.sources / "ftpiiu_plugin/ftpiiu.wps")

    def test_ftp_added_and_unapproved_build_refused(self):
        self.manifest.write_text(sync.ENTRY.sub(lambda m: "" if m[1] == sync.FTPIIU else m[0], self.manifest.read_text()))
        self.run_sync(True)
        self.run_sync()
        entry = next(m for m in sync.ENTRY.finditer(self.manifest.read_text()) if m[1] == sync.FTPIIU)
        self.assertEqual((entry[4], entry[5]), (str(1 << 31), "r"))
        before = self.manifest.read_bytes()
        (self.sources / "ftpiiu_plugin/ftpiiu.wps").write_bytes(b"unexpected build")
        with self.assertRaisesRegex(ValueError, "tested early-exit build"):
            self.run_sync(True)
        self.assertEqual(before, self.manifest.read_bytes())

    def run_sync(self, update=False):
        with contextlib.redirect_stdout(io.StringIO()):
            sync.synchronize(self.project, self.sources, update)

    def test_missing_plugin_stale_app_and_idempotence(self):
        with self.assertRaisesRegex(ValueError, "Stale native payload"):
            self.run_sync()
        self.run_sync(True)
        self.run_sync()
        entry = next(m for m in sync.ENTRY.finditer(self.manifest.read_text()) if m[1] == sync.REGION)
        self.assertEqual((entry[4], entry[5]), ("1017", "r"))
        for path in sync.RETURN_RUNTIME:
            entry = next(m for m in sync.ENTRY.finditer(self.manifest.read_text()) if m[1] == path)
            self.assertEqual((entry[4], entry[5]), (str(1 << 31), "r"))
        for path, mask in sync.cover_paths().items():
            entry = next(m for m in sync.ENTRY.finditer(self.manifest.read_text()) if m[1] == path)
            self.assertEqual((entry[4], entry[5]), (str(mask), "k"))
        stamp = self.manifest.stat().st_mtime_ns
        self.run_sync(True)
        self.assertEqual(stamp, self.manifest.stat().st_mtime_ns)
        app = self.project / "app/uFlow.wuhb"
        app.write_bytes(b"new app build")
        with self.assertRaisesRegex(ValueError, "Stale native payload"):
            self.run_sync()
        self.run_sync(True)
        self.run_sync()
        self.assertEqual((self.payload / "wiiu/apps/uFlow/uFlow.wuhb").read_bytes(), app.read_bytes())

    def test_raw_rpl_loader_refresh_survives_fresh_install(self):
        self.run_sync(True)
        path = "wiiu/environments/aroma/modules/setup/00_mocha.rpx"
        source = self.project / "payload/00_mocha.rpx"
        source.write_bytes(b"new raw-game early RPL loader build")
        with self.assertRaisesRegex(ValueError, "Stale native payload"):
            self.run_sync()
        self.run_sync(True)
        self.run_sync()
        self.assertEqual((self.payload / path).read_bytes(), source.read_bytes())
        entry = next(m for m in sync.ENTRY.finditer(self.manifest.read_text()) if m[1] == path)
        self.assertEqual((entry[2], entry[4], entry[5]), (sync.digest(source), str(1 << 31), "r"))

    def test_unapproved_plugin_refused(self):
        (self.sources / "region_free_plugin/regionfree.wps").write_bytes(b"stock plugin")
        with self.assertRaisesRegex(ValueError, "raw-metadata fix"):
            self.run_sync(True)
        self.assertFalse((self.payload / sync.REGION).exists())

    def test_plugin_backend_added_as_core_and_wrong_build_refused(self):
        text = self.manifest.read_text()
        self.manifest.write_text(sync.ENTRY.sub(lambda m: "" if m[1] == sync.PLUGIN_BACKEND else m[0], text))
        (self.payload / sync.PLUGIN_BACKEND).unlink()
        self.run_sync(True)
        self.run_sync()
        entry = next(m for m in sync.ENTRY.finditer(self.manifest.read_text()) if m[1] == sync.PLUGIN_BACKEND)
        self.assertEqual((entry[4], entry[5]), (str(1 << 31), "r"))
        before = self.manifest.read_bytes()
        (self.sources / "WiiUPluginLoaderBackend/PluginBackend.wms").write_bytes(b"unexpected build")
        with self.assertRaisesRegex(ValueError, "tested exit-trace build"):
            self.run_sync(True)
        self.assertEqual(before, self.manifest.read_bytes())

    def test_changed_base_refused_without_changes(self):
        before = self.manifest.read_bytes()
        (self.payload / "wiiu/vc-bases/nes/default/meta/meta.xml").write_bytes(b"changed base")
        with self.assertRaisesRegex(ValueError, "Unmanaged bundled asset"):
            self.run_sync(True)
        self.assertEqual(before, self.manifest.read_bytes())
        self.assertFalse((self.payload / sync.REGION).exists())

    def test_missing_component_refused(self):
        (self.project / "module/RPXLoadingModule.wms").unlink()
        with self.assertRaisesRegex(ValueError, "Build input missing"):
            self.run_sync(True)
        self.assertFalse((self.payload / sync.REGION).exists())

    def test_wiiflow_booters_added_and_refreshed(self):
        text = self.manifest.read_text()
        self.manifest.write_text(sync.ENTRY.sub(lambda m: "" if m[1] in sync.WIIFLOW_BIN_PATHS else m[0], text))
        for path in sync.WIIFLOW_BIN_PATHS:
            (self.payload / path).unlink()
        self.run_sync(True)
        self.run_sync()
        for path in sync.WIIFLOW_BIN_PATHS:
            entry = next(m for m in sync.ENTRY.finditer(self.manifest.read_text()) if m[1] == path)
            self.assertEqual((entry[4], entry[5]), ("6", "r"))
            source = sync.inputs(self.project, self.sources)[path]
            source.write_bytes(b"updated booter")
        with self.assertRaisesRegex(ValueError, "Stale native payload"):
            self.run_sync()
        self.run_sync(True)
        self.run_sync()
        for path in sync.WIIFLOW_BIN_PATHS:
            self.assertEqual((self.payload / path).read_bytes(), b"updated booter")

    def test_new_cover_manifest_entries(self):
        text = self.manifest.read_text()
        self.manifest.write_text(sync.ENTRY.sub(lambda m: "" if m[1] in sync.cover_paths() else m[0], text))
        self.run_sync(True)
        self.run_sync()
        for path in sync.cover_paths():
            self.assertIn(path, self.manifest.read_text())

    def test_wiiflow_forwarder_added_for_wii_and_gamecube(self):
        self.manifest.write_text(sync.ENTRY.sub(lambda m: "" if m[1] == sync.WIIFLOW_FORWARDER else m[0], self.manifest.read_text()))
        self.run_sync(True)
        self.run_sync()
        entry = next(m for m in sync.ENTRY.finditer(self.manifest.read_text()) if m[1] == sync.WIIFLOW_FORWARDER)
        self.assertEqual((entry[4], entry[5]), ("6", "r"))
        self.assertEqual((self.payload / sync.WIIFLOW_FORWARDER).read_bytes(), sync.inputs(self.project, self.sources)[sync.WIIFLOW_FORWARDER].read_bytes())


class DefaultCoverGeometryTest(unittest.TestCase):
    def test_real_default_panels_fit_system_cases(self):
        # Use real release art, not fixture files. This catches portrait defaults
        # accidentally shipped for landscape cases, and wrong spine widths.
        profiles = {
            "wiiu": (135, 190, 14), "wii": (135, 190, 14),
            "gamecube": (135, 190, 14), "ds": (135, 122, 15),
            "nes": (130, 178, 25), "snes": (178, 125, 30),
            "n64": (178, 125, 30), "gba": (127, 127, 25),
            "pce": (142, 125, 10), "msx": (135, 190, 25),
        }
        root = Path(__file__).resolve().parents[1] / "native-installer/assets/default-covers"
        self.assertEqual(set(profiles), set(sync.DEFAULT_COVERS))
        for system, (face_width, face_height, spine_width) in profiles.items():
            with self.subTest(system=system):
                png = (root / (system + ".png")).read_bytes()
                self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
                width, height = struct.unpack(">II", png[16:24])
                magic, length, back, front = (root / (system + ".layout")).read_text().split()
                self.assertEqual(magic, "UFLOW-SLEEVE-1")
                self.assertEqual(int(length), len(png))
                back, front = float(back), float(front)
                self.assertTrue(0 < back < front < 1)
                for part, fraction, physical in (("back", back, face_width),
                        ("spine", front-back, spine_width), ("front", 1-front, face_width)):
                    ratio = width * fraction / height / (physical / face_height)
                    self.assertLess(abs(ratio-1), 0.08, f"{system} {part} would be letterboxed: {ratio:.3f}")


if __name__ == "__main__":
    unittest.main()
