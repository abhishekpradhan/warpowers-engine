#!/usr/bin/env python3
"""Compile the actual D3DXLoadSurfaceFromSurface function against guarded surfaces.

Run with Python 3 and a native C++17 compiler (CXX selects it):
    python3 scripts/qa/test-surface-copy.py

The default tests the macOS/wasm manual filter path under ASan and UBSan.
--gli-root PATH additionally tests the Linux path with real GLI headers;
PATH must contain gli/gli.hpp (and usually external/glm/glm/glm.hpp).
--baseline REV requires the old equal-size copy to fail the padded-row test.
This is a CPU compatibility regression, not a GPU/browser integration test.
"""

import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SOURCE = "GeneralsMD/Code/CompatLib/Source/d3dx8_compat.cpp"
TOKENS = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)


def production_function(source):
    start = source.index("HRESULT WINAPI\nD3DXLoadSurfaceFromSurface(")
    depth = 0
    for token in TOKENS.finditer(source, source.index("{", start)):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return source[start:token.end()]
    raise ValueError("Cannot find the end of D3DXLoadSurfaceFromSurface")


def run_fixture(directory, label, source, compiler, gli_root=None, baseline=False):
    fixture = Path(__file__).with_name("surface-copy-fixture.cpp").read_text()
    translation_unit, binary = directory / f"{label}.cpp", directory / label
    translation_unit.write_text(fixture.replace("// INSERT_PRODUCTION_FUNCTION", production_function(source)))
    flags = ["-std=c++17", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g"]
    if gli_root:
        flags += ["-DTEST_GLI", "-DGLM_ENABLE_EXPERIMENTAL", "-I", str(gli_root)]
        if (gli_root / "external/glm/glm/glm.hpp").is_file():
            flags += ["-I", str(gli_root / "external/glm")]
    subprocess.run([*compiler, *flags, str(translation_unit), "-o", str(binary)], check=True)
    print(f"{label.upper()}: actual production surface-copy function", flush=True)
    return subprocess.run([str(binary), *( ["rows-only"] if baseline else [] )]).returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", help="require the earlier Git revision to fail the row-copy regression")
    parser.add_argument("--gli-root", type=Path, help="also exercise the Linux branch with these real GLI headers")
    args = parser.parse_args()
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    if not compiler:
        parser.error("CXX must name a native C++17 compiler")
    if args.gli_root and not (args.gli_root / "gli/gli.hpp").is_file():
        parser.error("--gli-root must contain gli/gli.hpp")
    with tempfile.TemporaryDirectory(prefix="surface-copy-") as directory:
        directory = Path(directory)
        if args.baseline:
            revision = subprocess.check_output(
                ["git", "rev-parse", "--verify", "--end-of-options", f"{args.baseline}^{{commit}}"],
                cwd=ROOT, text=True,
            ).strip()
            old = subprocess.check_output(["git", "show", f"{revision}:{SOURCE}"], cwd=ROOT, text=True)
            if run_fixture(directory, "baseline", old, compiler, baseline=True) != 1:
                print("FAIL: baseline must fail the row-copy check without crashing", file=sys.stderr)
                return 1
        current = (ROOT / SOURCE).read_text()
        if run_fixture(directory, "manual", current, compiler):
            return 1
        if args.gli_root and run_fixture(directory, "linux-gli", current, compiler, args.gli_root.resolve()):
            return 1
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Surface-copy regression could not run: {error}", file=sys.stderr)
        sys.exit(2)
