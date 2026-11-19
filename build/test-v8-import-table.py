#!/usr/bin/env python3
"""Host generator/linkage checks; native symbol lookup is tested separately."""
from pathlib import Path
import importlib.util
import subprocess
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('imports', Path(__file__).with_name('v8-import-table.py'))
imports = importlib.util.module_from_spec(spec)
spec.loader.exec_module(imports)

class ImportTableTest(unittest.TestCase):
    def test_invalid_or_empty_manifest(self):
        for text in ['', '# empty\n', 'bad-name', 'name";']:
            with self.assertRaises(ValueError):
                imports.generate(text)

    def test_weak_absence_and_defined_function_addresses(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root/'table.h').write_text(imports.generate('z_missing\na_present\na_present\n'))
            (root/'test.c').write_text('''
#include <string.h>
typedef void *APTR;
struct V8SymbolEntry { const char *name; APTR address; };
static int called;
void a_present(void) { called = 1; }
#include "table.h"
int main(void) {
    if (sizeof(v8ExactImports)/sizeof(v8ExactImports[0]) != 2) return 1;
    if (strcmp(v8ExactImports[0].name, "a_present")) return 2;
    if (strcmp(v8ExactImports[1].name, "z_missing")) return 3;
    if (!v8ExactImports[0].address || v8ExactImports[1].address) return 4;
    ((void (*)(void))v8ExactImports[0].address)();
    return called ? 0 : 5;
}
''')
            subprocess.run(['cc', '-Wall', '-Werror', str(root/'test.c'), '-o', str(root/'test')], check=True)
            subprocess.run([str(root/'test')], check=True)

if __name__ == '__main__':
    unittest.main()
