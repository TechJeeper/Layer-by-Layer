"""Packs the project as a CloudPebble import: dist/Layer-by-Layer-cloudpebble.zip."""
import os
import sys
import zipfile

root = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else '.')
os.makedirs(os.path.join(root, 'dist'), exist_ok=True)
out = os.path.join(root, 'dist', 'Layer-by-Layer-cloudpebble.zip')
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for item in ('package.json', 'package-lock.json', 'wscript', 'resources', 'src', 'node_modules'):
        path = os.path.join(root, item)
        if not os.path.exists(path):
            continue
        if os.path.isfile(path):
            z.write(path, 'Layer-by-Layer/' + item)
            continue
        for folder, _, files in os.walk(path):
            for name in sorted(files):
                full = os.path.join(folder, name)
                z.write(full, 'Layer-by-Layer/' + os.path.relpath(full, root).replace(os.sep, '/'))
print('Wrote %s (%d files)' % (out, len(z.namelist())))
