"""Exercise import handoff using an invented ELF, generation and native execution."""

import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


def fixture(invalid_scalar=False, tail_call=False, unknown_target=False, linked=False):
    base = 0x08804000
    data = bytearray(0x300)
    data[:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into("<HHIIIIIHHHHHH", data, 16,
                     2, 8, 1, base, 52, 0x200, 0, 52, 32, 1, 40, 3, 2)
    struct.pack_into("<IIIIIIII", data, 52, 1, 0x80, base, base, 0x180, 0x180, 5, 16)
    target = base + (0x100000 if unknown_target else 0x80)
    struct.pack_into("<IIII", data, 0x80,
                     ((2 if tail_call else 3) << 26) | (target >> 2), 0x24020007, 0x03E00008, 0)
    if invalid_scalar:
        struct.pack_into("<IIIIII", data, 0x80, 0x486800FF, 0x48E900FF,
                         (3 << 26) | ((base + 0x80) >> 2), 0x24020007, 0x03E00008, 0)
    if linked:
        # Exercise an unsuccessful SC, an LL/SC update, LL $zero (which must
        # skip its unmapped read), and SC $zero (which must still store).
        struct.pack_into("<" + "I" * 13, data, 0x80,
                         0x3C0A0880, 0x354A4400, 0x240B007B, 0xE14B0000,
                         0xC14B0000, 0x240B002A, 0xE14B0000, 0xC0000000,
                         0xE1400004, (3 << 26) | (target >> 2), 0x24020007, 0x03E00008, 0)
    struct.pack_into("<II", data, 0x100, 0x03E00008, 0x0000000C)
    # Module info points at one PSP import descriptor.
    data[0x184:0x18C] = b"Fixture\0"
    struct.pack_into("<II", data, 0x180 + 44, base + 0x140, base + 0x154)
    struct.pack_into("<IHHBBHII", data, 0x1C0,
                     base + 0x160, 0, 0, 5, 0, 1, base + 0x170, base + 0x80)
    data[0x1E0:0x1E8] = b"Fixture\0"
    struct.pack_into("<I", data, 0x1F0, 0x12345678)
    names = b"\0.sceModuleInfo\0.shstrtab\0"
    data[0x280:0x280 + len(names)] = names
    struct.pack_into("<IIIIIIIIII", data, 0x228,
                     1, 1, 2, base + 0x100, 0x180, 52, 0, 0, 4, 0)
    struct.pack_into("<IIIIIIIIII", data, 0x250,
                     16, 3, 0, 0, 0x280, len(names), 0, 0, 1, 0)
    return data


DRIVER = r'''
#include "psprecomp/elf32.hpp"
#include "psprecomp/runtime.hpp"
#include <cstdlib>
int main(int argc, char **argv) {
    if (argc != 6) return 1;
    psprecomp::Runtime runtime;
    auto elf = psprecomp::Elf32Image::from_file(argv[1]);
    elf.load_into(runtime.memory());
    psprecomp::register_generated_functions(runtime);
    runtime.cpu().gpr[8] = 123u;
    runtime.cpu().gpr[9] = 456u;
    runtime.cpu().vfpu_ctrl.fill(0xABCDEFu);
    runtime.memory().store32(0x08804404u, 999u);
    unsigned calls = 0;
    runtime.register_hle("Fixture", 0x12345678u,
        [&calls](psprecomp::Runtime &rt, psprecomp::AllegrexContext &) {
            ++calls;
            rt.stop("fixture import reached");
        });
    runtime.run(0x08804000u, 10u);
    if (argv[5][0] == '1' && (runtime.memory().load32(0x08804400u) != 42u ||
        runtime.memory().load32(0x08804404u) != 0u || runtime.cpu().gpr[11] != 1u || !runtime.cpu().load_linked)) return 2;
    if (argv[4][0] == '1') {
        return calls == 0 && runtime.cpu().pc == 0x08904000u &&
            runtime.stop_reason() == "No recompiled function registered at 0x08904000" ? 0 : 1;
    }
    return calls == 1 && runtime.cpu().gpr[2] == 7u &&
        runtime.cpu().gpr[31] == std::strtoul(argv[2], nullptr, 0) &&
        runtime.cpu().gpr[8] == (argv[3][0] == '1' ? 0u : 123u) &&
        runtime.cpu().vfpu_ctrl[15] == 0xABCDEFu &&
        runtime.stop_reason() == "fixture import reached" ? 0 : 1;
}
'''


class ImportCodegenTests(unittest.TestCase):
    def check_import(self, span, invalid_scalar=False, tail_call=False, unknown_target=False, linked=False):
        with tempfile.TemporaryDirectory(prefix="psprecomp-import-") as temp:
            root = Path(temp)
            elf = root / "fixture.elf"
            elf.write_bytes(fixture(invalid_scalar, tail_call, unknown_target, linked))
            output = root / "generated"
            # The regression was an unbounded code-emission loop at a JAL to an
            # import. A timeout makes that exact failure fast and deterministic.
            subprocess.run([ARGS.codegen, str(elf), "--auto", str(output),
                            "0x08804000", span], check=True, timeout=5,
                           capture_output=True, text=True)
            sources = list(output.glob("*.cpp"))
            self.assertLess(sum(p.stat().st_size for p in sources), 100_000)
            driver = root / "driver.cpp"
            driver.write_text(DRIVER)
            binary = root / "import_test"
            compiled = subprocess.run([ARGS.compiler, "-std=c++20", "-O0", "-I", ARGS.include,
                            str(driver), *(str(p) for p in sources), ARGS.core,
                            "-o", str(binary)], timeout=30,
                           capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            return_address = "0x0880402C" if linked else "0" if tail_call else "0x08804010" if invalid_scalar else "0x08804008"
            result = subprocess.run([str(binary), str(elf), return_address,
                                     str(int(invalid_scalar)), str(int(unknown_target)), str(int(linked))], timeout=5,
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_cross_unit_import_handoff(self):
        self.check_import("0x40")

    def test_same_unit_import_handoff(self):
        self.check_import("0x4000")

    def test_same_unit_tail_import_handoff(self):
        self.check_import("0x4000", tail_call=True)

    def test_cross_unit_tail_import_handoff(self):
        self.check_import("0x40", tail_call=True)

    def test_invalid_scalar_encoding_compiles(self):
        self.check_import("0x4000", invalid_scalar=True)

    def test_load_linked_store_conditional(self):
        self.check_import("0x4000", linked=True)

    def test_unknown_direct_call_reports_missing_code(self):
        self.check_import("0x4000", unknown_target=True)

    def test_unknown_direct_jump_reports_missing_code(self):
        self.check_import("0x4000", unknown_target=True, tail_call=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    for name in ("codegen", "compiler", "include", "core"):
        parser.add_argument("--" + name, required=True)
    ARGS = parser.parse_args()
    unittest.main(argv=[__file__])
