import zipfile
import os
from pathlib import Path

def should_ignore(file_path):
    name = file_path.name.lower()
    return ('conflicted' in name or 
            name.endswith('.log') or 
            name.endswith('.config'))

version = input("Enter version name: ")
zip_name = f"MusicReader_{version}.zip"
source_dir = Path('./bin')

with zipfile.ZipFile(zip_name, 'w', zipfile.ZIP_DEFLATED) as zipf:
    for file_path in source_dir.rglob('*'):
        if file_path.is_file() and not should_ignore(file_path):
            arcname = Path('MusicReader') / file_path.relative_to(source_dir)
            print(f"Adding {file_path} as {arcname}")
            zipf.write(file_path, arcname)

print(f"Created {zip_name}")