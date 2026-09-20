"""Keep raw-title hooks out of Wii U Menu and Root bootstrap processes."""
from pathlib import Path
import re
import unittest


class RawHookScopeTest(unittest.TestCase):
    def test_raw_callbacks_only_target_game(self):
        source = (Path(__file__).resolve().parents[1] / "module/src/RPXLoading.cpp").read_text()
        table = source.split("function_replacement_data_t rpx_utils_function_replacements[] = {", 1)[1].split("};", 1)[0]
        callbacks = set(re.findall(r"DECL_FUNCTION\([^,]+,\s*(Raw_\w+)", source))
        registrations = re.findall(r"(REPLACE_FUNCTION\w*)\((Raw_\w+),([^\n]+)\)", table)
        self.assertTrue(callbacks)
        self.assertEqual(callbacks, {name for _, name, _ in registrations})
        self.assertEqual(len(callbacks), len(registrations))
        for macro, name, arguments in registrations:
            with self.subTest(callback=name):
                self.assertEqual(macro, "REPLACE_FUNCTION_FOR_PROCESS")
                self.assertEqual(arguments.split(",")[-1].strip(), "FP_TARGET_PROCESS_GAME")
        self.assertIn("UFlow_ProcUIProcessMessages, LIBRARY_PROC_UI, ProcUIProcessMessages, FP_TARGET_PROCESS_WII_U_MENU", table)
        self.assertNotIn("UFLOW_DISABLE_MENU_RETURN_HOOK", source)


if __name__ == "__main__":
    unittest.main()
