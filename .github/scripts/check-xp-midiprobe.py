"""Check the static loader dependencies of the midiprobe artifact (python3-pefile)."""

import sys
from pathlib import Path

import pefile


with pefile.PE(sys.argv[1], fast_load=True) as pe:
    header = pe.OPTIONAL_HEADER
    if (pe.FILE_HEADER.Machine != pefile.MACHINE_TYPE['IMAGE_FILE_MACHINE_I386']
            or header.Magic != pefile.OPTIONAL_HEADER_MAGIC_PE
            or (header.MajorSubsystemVersion, header.MinorSubsystemVersion) != (5, 1)):
        sys.exit('Expected an x86 PE32 executable with subsystem version 5.1')

    delay = header.DATA_DIRECTORY[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT']]
    if delay.VirtualAddress or delay.Size:
        sys.exit('Delay imports have not been reviewed for XP')

    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
    if pe.get_warnings():
        sys.exit('\n'.join(pe.get_warnings()))

    allowed = set(Path(__file__).with_name('xp-sp3-x86-imports.txt').read_text().splitlines())
    imported = set()
    for entry in pe.DIRECTORY_ENTRY_IMPORT:
        dll = entry.dll.decode('ascii').lower()
        for symbol in entry.imports:
            name = f'#{symbol.ordinal}' if symbol.import_by_ordinal else symbol.name.decode('ascii')
            imported.add(f'{dll}!{name}')

    if unknown := imported - allowed:
        sys.exit('Unreviewed XP imports:\n' + '\n'.join(sorted(unknown)))
    if not any(pair.startswith('winmm.dll!') for pair in imported):
        sys.exit('Expected a WinMM import')

    print(f'{sys.argv[1]}: verified {len(imported)} distinct XP imports')
