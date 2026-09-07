#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the production keyboard methods against deterministic input fixtures.

Run from any directory with Python 3 and a native C++17 compiler:
    python3 scripts/qa/test-keyboard-modifiers.py

CXX selects the compiler (default: c++). Optional --baseline REV also verifies
that an earlier revision fails the behavioral checks. No engine build, SDL,
browser or game assets are needed; this does not test those integration layers.
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
SOURCE = "Core/GameEngine/Source/GameClient/Input/Keyboard.cpp"
SIGNATURES = (
    "void Keyboard::updateKeys()",
    "Bool Keyboard::checkKeyRepeat()",
    "WideChar Keyboard::translateKey( WideChar keyCode )",
)
# Ignore braces in comments and ordinary literals when finding method ends.
TOKENS = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)


def production_method(source, signature):
    """Extract actual production code; never maintain a second input algorithm."""
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for token in TOKENS.finditer(source, opening):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return source[start:token.end()]
    raise ValueError(f"Cannot find the end of {signature}")


def run_fixture(directory, label, source, compiler):
    fixture = Path(__file__).with_name("keyboard-modifiers-fixture.cpp").read_text()
    methods = "\n\n".join(production_method(source, signature) for signature in SIGNATURES)
    translation_unit = directory / f"{label}.cpp"
    binary = directory / label
    translation_unit.write_text(fixture.replace("// INSERT_PRODUCTION_METHODS", methods))
    subprocess.run([*compiler, "-std=c++17", str(translation_unit), "-o", str(binary)], check=True)
    print(f"{label.upper()}: production keyboard methods", flush=True)
    return subprocess.run([str(binary)]).returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", help="also require behavioral failure from this earlier Git revision")
    args = parser.parse_args()
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    if not compiler:
        parser.error("CXX must name a native C++17 compiler")
    with tempfile.TemporaryDirectory(prefix="keyboard-modifiers-") as directory:
        directory = Path(directory)
        if args.baseline:
            revision = subprocess.check_output(
                ["git", "rev-parse", "--verify", "--end-of-options", f"{args.baseline}^{{commit}}"],
                cwd=ROOT, text=True,
            ).strip()
            source = subprocess.check_output(["git", "show", f"{revision}:{SOURCE}"], cwd=ROOT, text=True)
            if run_fixture(directory, "baseline", source, compiler) != 1:
                print("FAIL: baseline must fail the behavioral checks, without crashing", file=sys.stderr)
                return 1
        if run_fixture(directory, "current", (ROOT / SOURCE).read_text(), compiler) != 0:
            return 1
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Keyboard regression could not run: {error}", file=sys.stderr)
        sys.exit(2)
