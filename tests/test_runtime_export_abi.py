"""Every function in a RuntimeExport table is ABI (sysv_abi): guest code calls it with the System V ABI.

The table stores void*, so the compiler accepts any function there. On Windows a missing ABI means
the host function reads its arguments from the wrong registers: silent corruption, not an error.
clang's AST records the convention in each function's type, so the check reads it from there.
"""
from paths import ROOT
from pathlib import Path
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

CLANG = os.environ.get('CLANG') or shutil.which('clang')
PKG_CONFIG = shutil.which('pkg-config')
PACKAGES = ['vulkan', 'sdl3', 'libavcodec', 'libavutil', 'libswscale']  # as build.sh
TABLE = re.compile(r'\bRuntimeExport\s+(\w+)\s*\[')


def is_abi(qual_type, windows):
    # Windows: the attribute itself (MS is the default convention there). Elsewhere sysv_abi is the
    # default and clang drops it from the type, but keeps the ABI macro it was written with.
    return '__attribute__((sysv_abi))' in qual_type if windows else bool(re.match(r'ABI\b', qual_type))


def walk(node):
    if isinstance(node, dict):
        yield node
        for value in node.values():
            yield from walk(value)
    elif isinstance(node, list):
        for value in node:
            yield from walk(value)


def tables(ast_text, names):
    """RuntimeExport VarDecls in a JSON AST dump (-ast-dump-filter prints one object per match)."""
    decoder, i = json.JSONDecoder(), 0
    while (i := ast_text.find('{', i)) >= 0:
        node, i = decoder.raw_decode(ast_text, i)
        if (node.get('kind') == 'VarDecl' and node.get('name') in names
                and 'RuntimeExport' in node.get('type', {}).get('qualType', '')):
            yield node


def entries(table):
    """(export name, function initializer) for each {name, function} of the table."""
    for init in table.get('inner', []):
        for entry in init.get('inner', []) if init.get('kind') == 'InitListExpr' else []:
            fields = entry.get('inner', [])
            if entry.get('kind') == 'InitListExpr' and len(fields) == 2:
                names = [n['value'].strip('"') for n in walk(fields[0]) if n.get('kind') == 'StringLiteral']
                yield (names[0] if names else '?'), fields[1]


def is_null(node):
    """{name, NULL}: nothing to call."""
    return all(n.get('value', '0') == '0' for n in walk(node) if n.get('kind') == 'IntegerLiteral') and \
        not any(n.get('kind') == 'DeclRefExpr' for n in walk(node))


def clang(flags, source, *options):
    return subprocess.run([CLANG, '-w', *flags, *options, str(source)],
                          capture_output=True, text=True, encoding='utf-8', errors='replace')


def violations(source, flags, windows):
    """Problems in one C file's RuntimeExport tables, and how many exported functions were checked."""
    names = set(TABLE.findall(source.read_text(encoding='utf-8', errors='replace')))
    problems, checked = [], 0
    for name in sorted(names):
        r = clang(flags, source, '-fsyntax-only', '-Xclang', '-ast-dump=json', '-Xclang', f'-ast-dump-filter={name}')
        if r.returncode:
            problems.append(f'{source.name}: clang failed:\n{r.stderr[-2000:]}')
            continue
        found = [t for t in tables(r.stdout, names) if t['name'] == name]
        if not found:
            # Absent from the AST is fine only when this platform's build leaves the table out.
            pre = clang(flags, source, '-E', '-P')
            if pre.returncode or re.search(rf'\bRuntimeExport\s+{name}\s*\[', pre.stdout):
                problems.append(f'{source.name}: RuntimeExport table {name} not found in the AST')
            continue
        for table in found:
            for export, node in entries(table):
                decls = [n['referencedDecl'] for n in walk(node) if n.get('kind') == 'DeclRefExpr']
                if not decls:
                    if not is_null(node):
                        problems.append(f'{source.name}: {name}["{export}"] is not a function name')
                    continue
                decl = decls[0]
                if decl['kind'] != 'FunctionDecl':
                    continue  # data export (&variable)
                checked += 1
                if not is_abi(decl['type']['qualType'], windows):
                    problems.append(f'{source.name}: {name}["{export}"] -> {decl["name"]} is not ABI: '
                                    f'{decl["type"]["qualType"]}')
    return problems, checked


FIXTURE = r'''
#define ABI __attribute__((sysv_abi))
typedef struct { const char *name; void *function; } RuntimeExport;
static ABI int good(int a) { return a; }
static int bad(int a) { return a; }
ABI int declared(void);
int declared(void) { return 0; }
static int value;
static const RuntimeExport exports[]={ {"good",good}, {"bad",bad}, {"cast",(void *)bad},
    {"declared",declared}, {"data",&value}, {"none",0} };
#if 0
static const RuntimeExport other[]={ {"bad",bad} };
#endif
'''


@unittest.skipUnless(CLANG, 'needs clang (its AST records calling conventions)')
class RuntimeExportAbiTests(unittest.TestCase):

    def test_check_catches_missing_abi(self):
        # The check itself, for both conventions: a missing ABI is reported, through a cast too;
        # an ABI prototype covers its definition; data, NULL and compiled-out tables are not errors.
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / 'fixture.c'
            source.write_text(FIXTURE)
            for target, windows in (('x86_64-w64-windows-gnu', True),):
                with self.subTest(target=target):
                    problems, checked = violations(source, [f'--target={target}'], windows)
                    self.assertEqual(checked, 4, problems)
                    self.assertEqual(sorted(p.split('"')[1] for p in problems), ['bad', 'cast'], problems)

    @unittest.skipUnless(PKG_CONFIG, 'needs pkg-config for the runtime include flags (as build.sh)')
    def test_runtime_exports_are_abi(self):
        cflags = subprocess.run([PKG_CONFIG, '--cflags', *PACKAGES], capture_output=True, text=True, check=True)
        flags = ['-std=c11', '-DPS4RT_RUNTIME', f'-I{ROOT}', f'-I{ROOT / "src"}', *cflags.stdout.split()]
        windows = sys.platform == 'win32'
        if windows:
            flags += ['-DNOMINMAX', '-DWIN32_LEAN_AND_MEAN']  # ps4rtgpu's public definitions
        sources = [s for s in sorted((ROOT / 'src').glob('*.c'))
                   if TABLE.search(s.read_text(encoding='utf-8', errors='replace'))]
        problems, checked = [], 0
        for source in sources:
            p, n = violations(source, flags, windows)
            problems += p
            checked += n
        self.assertFalse(problems, '\n'.join(problems))
        self.assertGreater(checked, 100, f'only {checked} exported functions checked in {len(sources)} files')


if __name__ == '__main__':
    unittest.main()
