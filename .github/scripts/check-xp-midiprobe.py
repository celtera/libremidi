"""Check the static loader dependencies of the XP executables (python3-pefile).

Usage: check-xp-midiprobe.py <exe>...
"""

import sys
from pathlib import Path

import pefile


def allowed_imports():
    lines = Path(__file__).with_name('xp-sp3-x86-imports.txt').read_text().splitlines()
    # A comment starts its line with "# "; an ordinal import would be "dll!#n".
    return {line for line in lines if line and not line.startswith('#')}


def check(path, allowed):
    with pefile.PE(path, fast_load=True) as pe:
        header = pe.OPTIONAL_HEADER
        if (pe.FILE_HEADER.Machine != pefile.MACHINE_TYPE['IMAGE_FILE_MACHINE_I386']
                or header.Magic != pefile.OPTIONAL_HEADER_MAGIC_PE
                or (header.MajorSubsystemVersion, header.MinorSubsystemVersion) != (5, 1)):
            sys.exit(f'{path}: expected an x86 PE32 executable with subsystem version 5.1')

        delay = header.DATA_DIRECTORY[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT']]
        if delay.VirtualAddress or delay.Size:
            sys.exit(f'{path}: delay imports have not been reviewed for XP')

        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
        if pe.get_warnings():
            sys.exit(f'{path}:\n' + '\n'.join(pe.get_warnings()))

        imported = set()
        for entry in pe.DIRECTORY_ENTRY_IMPORT:
            dll = entry.dll.decode('ascii').lower()
            for symbol in entry.imports:
                name = f'#{symbol.ordinal}' if symbol.import_by_ordinal else symbol.name.decode('ascii')
                imported.add(f'{dll}!{name}')

        if unknown := imported - allowed:
            sys.exit(f'{path}: unreviewed XP imports:\n' + '\n'.join(sorted(unknown)))
        if not any(pair.startswith('winmm.dll!') for pair in imported):
            sys.exit(f'{path}: expected a WinMM import')

        print(f'{path}: verified {len(imported)} distinct XP imports')


if len(sys.argv) < 2:
    sys.exit(__doc__)
allowed = allowed_imports()
for executable in sys.argv[1:]:
    check(executable, allowed)
