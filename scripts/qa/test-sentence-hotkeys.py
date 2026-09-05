#!/usr/bin/env python3
"""Check production sentence layout and glyph blitting with address/UB sanitizers.

Run from any directory with Python 3 and a native C++17 compiler supporting
AddressSanitizer and UndefinedBehaviorSanitizer:
    python3 scripts/qa/test-sentence-hotkeys.py

CXX selects the compiler (default: c++). Optional --baseline REV requires the
earlier revision to reproduce both trailing-ampersand overreads and the nullable
hotkey-output failure. The fixture supplies deterministic font/surface contracts;
it does not exercise FreeType, the graphics bridge, or the browser Retry flow.
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
SOURCE = "Core/Libraries/Source/WWVegas/WW3D2/render2dsentence.cpp"
SIGNATURES = (
    "void\nRender2DSentenceClass::Allocate_New_Surface",
    "void\tRender2DSentenceClass::Build_Sentence_Centered",
    "Vector2\tRender2DSentenceClass::Build_Sentence_Not_Centered",
    "void\nRender2DSentenceClass::Build_Sentence (",
    "void\nFontCharsClass::Blit_Char",
)
# Ignore braces in comments and ordinary literals when finding method ends.
TOKENS = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)


def production_method(source, signature):
    """Compile the actual methods, with production line numbers in diagnostics."""
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for token in TOKENS.finditer(source, opening):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                line = source.count("\n", 0, start) + 1
                return f'#line {line} "{SOURCE}"\n' + source[start:token.end()]
    raise ValueError(f"Cannot find the end of {signature}")


def compile_fixture(directory, label, source, compiler):
    fixture = Path(__file__).with_name("sentence-hotkeys-fixture.cpp").read_text()
    methods = "\n\n".join(production_method(source, signature) for signature in SIGNATURES)
    translation_unit = directory / f"{label}.cpp"
    binary = directory / label
    translation_unit.write_text(fixture.replace("// INSERT_PRODUCTION_METHODS", methods))
    subprocess.run([
        *compiler, "-std=c++17", "-O1", "-g", "-fsanitize=address,undefined",
        "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-Wno-unused-value",
        str(translation_unit), "-o", str(binary),
    ], check=True)
    return binary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", help="also require sanitizer failures from this earlier Git revision")
    args = parser.parse_args()
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    if not compiler:
        parser.error("CXX must name a native C++17 compiler")

    with tempfile.TemporaryDirectory(prefix="sentence-hotkeys-") as directory:
        directory = Path(directory)
        if args.baseline:
            revision = subprocess.check_output(
                ["git", "rev-parse", "--verify", "--end-of-options", f"{args.baseline}^{{commit}}"],
                cwd=ROOT, text=True,
            ).strip()
            source = subprocess.check_output(["git", "show", f"{revision}:{SOURCE}"], cwd=ROOT, text=True)
            baseline = compile_fixture(directory, "baseline", source, compiler)
            failures = (
                ("left-trailing-word", "AddressSanitizer: heap-buffer-overflow"),
                ("centered-single-ampersand", "AddressSanitizer: heap-buffer-overflow"),
                ("left-x-only", "runtime error: store to null pointer"),
                ("centered-x-only", "runtime error: store to null pointer"),
            )
            for case, diagnostic in failures:
                result = subprocess.run([str(baseline), case], capture_output=True, text=True, timeout=20)
                if result.returncode == 0 or diagnostic not in result.stderr:
                    print(result.stdout + result.stderr, file=sys.stderr)
                    print(f"FAIL: baseline did not reproduce {case}: {diagnostic}", file=sys.stderr)
                    return 1
                print(f"BASELINE: {case} reproduced {diagnostic}", flush=True)

        current = compile_fixture(directory, "current", (ROOT / SOURCE).read_text(), compiler)
        return subprocess.run([str(current)], timeout=30).returncode


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"Sentence regression could not run: {error}", file=sys.stderr)
        sys.exit(2)
