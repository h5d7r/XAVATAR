"""Assemble the self-contained ELF bundle and the editable source archive."""
from pathlib import Path
import shutil
import zipfile
import hashlib
import re

root = Path(__file__).resolve().parents[1]
output = root / 'dist' / 'XAVATAR'
output.mkdir(parents=True, exist_ok=True)
for name in ('XAVATAR.elf', 'XAVATAR-installer.elf'):
    payload = root / 'build' / name
    if not payload.is_file():
        raise SystemExit('Build the PS4 targets first: make')
    shutil.copy2(payload, output / name)
shutil.copy2(root / 'payload_sender.py', output / 'payload_sender.py')
(output / 'assets').mkdir(exist_ok=True)
shutil.copy2(root / 'assets/logo.png', output / 'assets/logo.png')
# Retire only the known frontend copies produced by the old packaging layout.
for name in ('ui.html', 'styles.css', 'app.js'):
    old = output / 'data' / 'XAVATAR' / name
    if not old.resolve().is_relative_to(output.resolve()):
        raise SystemExit(f'Unexpected old bundle path: {old}')
    old.unlink(missing_ok=True)
for old in (output / 'data' / 'XAVATAR', output / 'data'):
    if not old.resolve().is_relative_to(output.resolve()):
        raise SystemExit(f'Unexpected old bundle directory: {old}')
    if old.is_dir() and not any(old.iterdir()):
        old.rmdir()
shutil.copy2(root / 'README.md', output / 'README.md')
shutil.copy2(root / 'THIRD_PARTY.md', output / 'THIRD_PARTY.md')
shutil.copy2(root / 'LICENSE', output / 'LICENSE')
version = re.search(r'#define XA_VERSION "([^"]+)"', (root / 'include/xavatar.h').read_text()).group(1)
zip_path = root / 'dist' / f'XAVATAR-{version}.zip'
with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as archive:
    for name in ('XAVATAR.elf', 'XAVATAR-installer.elf', 'payload_sender.py', 'README.md', 'THIRD_PARTY.md', 'LICENSE'):
        archive.write(output / name, Path('XAVATAR') / name)
    archive.write(output / 'assets/logo.png', Path('XAVATAR/assets/logo.png'))
print(zip_path)

source_zip = root / 'dist' / 'XAVATAR-source.zip'
with zipfile.ZipFile(source_zip, 'w', zipfile.ZIP_DEFLATED) as archive:
    for name in ('README.md', 'LICENSE', 'THIRD_PARTY.md', 'Makefile', '.gitignore', '.gitattributes', 'build.ps1', 'payload_sender.py', 'ui.html', 'styles.css', 'app.js'):
        archive.write(root / name, Path('XAVATAR-source') / name)
    for name in ('source', 'include', 'vendor', 'tools', 'assets', '.github'):
        for path in sorted((root / name).rglob('*')):
            if path.is_file() and '__pycache__' not in path.parts:
                archive.write(path, Path('XAVATAR-source') / path.relative_to(root))
print(source_zip)

checksums = []
for path in (root / 'build/XAVATAR.elf', root / 'build/XAVATAR-installer.elf', zip_path, source_zip):
    checksums.append(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + path.name)
(root / 'dist/SHA256SUMS.txt').write_text('\n'.join(checksums) + '\n', encoding='ascii')
