#!/usr/bin/env python3
"""Compile the production D3DX mip filter against observable surface references.

Needs Python 3 and a native C++17 compiler (CXX, default c++). No engine/GPU
build is required. --baseline REV also requires the previous filter to fail
the ownership checks. This verifies reference and error contracts, not pixels
or the browser's complete Retry lifecycle.
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
SOURCE = 'GeneralsMD/Code/CompatLib/Source/d3dx8_compat.cpp'
TOKENS = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)


def method(source):
    start = source.index('HRESULT WINAPI\nD3DXFilterTexture(')
    depth = 0
    for token in TOKENS.finditer(source, source.index('{', start)):
        if token.group() == '{':
            depth += 1
        elif token.group() == '}':
            depth -= 1
            if not depth:
                return source[start:token.end()]
    raise ValueError('Cannot extract D3DXFilterTexture')


def run(directory, label, source, compiler):
    fixture = Path(__file__).with_name('mip-filter-fixture.cpp').read_text()
    unit, binary = directory / f'{label}.cpp', directory / label
    unit.write_text(fixture.replace('// INSERT_PRODUCTION_METHODS', method(source)))
    subprocess.run([*compiler, '-std=c++17', '-Wall', '-Wextra', str(unit), '-o', str(binary)], check=True)
    print(label.upper(), flush=True)
    return subprocess.run([str(binary), *(['baseline'] if label == 'baseline' else [])]).returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline')
    args = parser.parse_args()
    compiler = shlex.split(os.environ.get('CXX', 'c++'))
    if not compiler:
        parser.error('CXX must name a native C++17 compiler')
    with tempfile.TemporaryDirectory(prefix='mip-filter-') as temporary:
        directory = Path(temporary)
        if args.baseline:
            revision = subprocess.check_output(['git', 'rev-parse', '--verify', '--end-of-options',
                                               f'{args.baseline}^{{commit}}'], cwd=ROOT, text=True).strip()
            baseline = subprocess.check_output(['git', 'show', f'{revision}:{SOURCE}'], cwd=ROOT, text=True)
            if run(directory, 'baseline', baseline, compiler) != 1:
                raise ValueError('Baseline must fail the ownership checks without crashing')
        return run(directory, 'current', (ROOT / SOURCE).read_text(), compiler)


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f'Mip filter regression could not run: {error}', file=sys.stderr)
        sys.exit(2)
