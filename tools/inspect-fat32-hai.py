"""Read-only FAT32/NFS extent audit; does not install or modify a title.

The report is diagnostic, NOT a launch map: moving files invalidates extents.
Run on Windows with a mounted USB volume, e.g. E: and a relative title path.
"""
import argparse
import functools
import json
import pathlib
import struct


class Fat32:
    def __init__(self, volume):
        self.file = open("\\\\.\\" + volume, "rb", buffering=0)
        try:
            boot = self.read(0, 512)
            self.sector = struct.unpack_from("<H", boot, 11)[0]
            self.cluster_sectors = boot[13]
            reserved = struct.unpack_from("<H", boot, 14)[0]
            self.total = struct.unpack_from("<I", boot, 32)[0]
            fat_size = struct.unpack_from("<I", boot, 36)[0]
            flags = struct.unpack_from("<H", boot, 40)[0]
            fats = boot[16]
            active = flags & 15 if flags & 128 else 0
            self.hidden = struct.unpack_from("<I", boot, 28)[0]
            self.root = struct.unpack_from("<I", boot, 44)[0]
            if (boot[510:] != b"\x55\xaa" or self.sector != 512 or
                    not self.cluster_sectors or self.cluster_sectors & (self.cluster_sectors-1) or
                    self.cluster_sectors > 128 or not fats or active >= fats or
                    not fat_size or not reserved or struct.unpack_from("<H", boot, 17)[0]):
                raise ValueError("unsupported FAT32 geometry")
            self.fat = reserved + active * fat_size
            self.data = reserved + fats * fat_size
            self.cluster_count = (self.total - self.data) // self.cluster_sectors
            if self.cluster_count < 65525 or self.cluster_count + 2 > fat_size * 128:
                raise ValueError("invalid FAT32 capacity")
        except Exception:
            self.file.close()
            raise

    def read(self, offset, size):
        self.file.seek(offset)
        data = self.file.read(size)
        if len(data) != size:
            raise ValueError("short volume read")
        return data

    @functools.lru_cache(maxsize=4096)
    def fat_sector(self, index):
        return self.read((self.fat + index) * 512, 512)

    def chain(self, first):
        visited = set()
        cluster = first
        while cluster < 0x0ffffff8:
            if cluster < 2 or cluster >= self.cluster_count + 2 or cluster in visited:
                raise ValueError("invalid or cyclic FAT chain")
            visited.add(cluster)
            yield cluster
            cluster = struct.unpack_from("<I", self.fat_sector(cluster // 128), (cluster % 128)*4)[0] & 0x0fffffff

    def entries(self, first):
        long_name = []
        for cluster in self.chain(first):
            sector = self.data + (cluster-2)*self.cluster_sectors
            raw = self.read(sector*512, self.cluster_sectors*512)
            for pos in range(0, len(raw), 32):
                slot = raw[pos:pos+32]
                if slot[0] == 0:
                    return
                if slot[0] == 0xe5:
                    long_name = []
                    continue
                if slot[11] == 15:
                    if slot[0] & 64:
                        long_name = []
                    long_name.append(slot)
                    continue
                name = slot[:8].decode("cp437").rstrip()
                ext = slot[8:11].decode("cp437").rstrip()
                if ext:
                    name += "." + ext
                if long_name:
                    checksum = 0
                    for byte in slot[:11]:
                        checksum = (((checksum & 1) << 7) + (checksum >> 1) + byte) & 255
                    expected = len(long_name)
                    if (long_name[0][0] != (64 | expected) or
                            any((s[0] & 31) != expected-i or s[13] != checksum for i, s in enumerate(long_name))):
                        raise ValueError("invalid long filename sequence")
                    encoded = b"".join(s[1:11]+s[14:26]+s[28:32] for s in reversed(long_name))
                    name = encoded.decode("utf-16-le").split("\0")[0].rstrip("\uffff")
                long_name = []
                if not slot[11] & 8:
                    first_cluster = (struct.unpack_from("<H", slot, 20)[0] << 16) | struct.unpack_from("<H", slot, 26)[0]
                    yield name, slot[11], first_cluster, struct.unpack_from("<I", slot, 28)[0]

    def directory(self, relative):
        cluster = self.root
        for part in relative.replace("\\", "/").strip("/").split("/"):
            if not part or part in (".", ".."):
                raise ValueError("invalid relative path")
            matches = [entry for entry in self.entries(cluster) if entry[0].casefold() == part.casefold()]
            if len(matches) != 1 or not matches[0][1] & 16:
                raise ValueError("directory missing or ambiguous: " + part)
            cluster = matches[0][2]
        return cluster


def inspect(volume, title):
    fat = Fat32(volume)
    try:
        content = fat.directory(title + "/content")
        files = sorted(e for e in fat.entries(content) if e[0].lower().endswith(".nfs"))
        if not files:
            raise ValueError("no NFS files")
        report = {"sector_size":512, "cluster_sectors":fat.cluster_sectors,
                  "partition_start":fat.hidden, "files":[]}
        all_extents = []
        for index, (name, attr, first, size) in enumerate(files):
            if name != f"hif_{index:06d}.nfs" or attr & 16 or not size or size % 512:
                raise ValueError("invalid NFS sequence, size or type: " + name)
            remaining = size // 512
            extents = []
            for cluster in fat.chain(first):
                if not remaining:
                    raise ValueError("FAT chain exceeds file allocation")
                count = min(remaining, fat.cluster_sectors)
                start = fat.hidden + fat.data + (cluster-2)*fat.cluster_sectors
                if extents and extents[-1][0] + extents[-1][1] == start:
                    extents[-1][1] += count
                else:
                    extents.append([start, count])
                remaining -= count
            if remaining:
                raise ValueError("FAT chain is shorter than file")
            # Independently compare the first/last sectors through Windows' file
            # API with the calculated raw locations, without reading all GBs.
            path = pathlib.Path(volume + "\\") / title / "content" / name
            with path.open("rb") as file:
                for logical, physical in [(0, extents[0][0]),
                                           (size-512, sum(extents[-1])-1)]:
                    file.seek(logical)
                    if file.read(512) != fat.read((physical-fat.hidden)*512, 512):
                        raise ValueError("raw/file address comparison failed")
            all_extents.extend(extents)
            report["files"].append({"name":name,"bytes":size,"extents":extents})
        physical = sorted(all_extents)
        if any(a[0]+a[1] > b[0] for a,b in zip(physical,physical[1:])):
            raise ValueError("NFS files have overlapping physical allocation")
        report["total_bytes"] = sum(f["bytes"] for f in report["files"])
        report["extent_count"] = len(all_extents)
        return report
    finally:
        fat.file.close()
        fat.fat_sector.cache_clear()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("volume")
    parser.add_argument("title")
    args = parser.parse_args()
    if len(args.volume) != 2 or not args.volume[0].isalpha() or args.volume[1] != ":":
        parser.error("volume must be a drive letter, e.g. E:")
    print(json.dumps(inspect(args.volume, args.title), indent=2))
