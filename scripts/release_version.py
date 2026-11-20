"""The release name, read from the one place it is written: T3_VERSION in src/saturn/version.h."""
import re
from pathlib import Path

HEADER = Path(__file__).resolve().parents[1] / 'src/saturn/version.h'


def version():
    found = re.search(r'#define\s+T3_VERSION\s+"([^"]+)"', HEADER.read_text())
    if found is None:
        raise ValueError(f'no T3_VERSION in {HEADER}')
    return found.group(1)
