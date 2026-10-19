#!/usr/bin/env python3
"""Bundle transitive Mach-O dependencies; reject unresolved external libraries."""
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

app = Path(sys.argv[1]).resolve()
frameworks = app / 'Contents/Frameworks'
frameworks.mkdir(parents=True, exist_ok=True)
executable_dir = app / 'Contents/MacOS'
queue = []
copied = {}

def output(*args):
    return subprocess.check_output(args, text=True)

def macho(path):
    return 'Mach-O' in output('/usr/bin/file', '-b', str(path))

for folder in ('MacOS', 'PlugIns'):
    for path in (app / 'Contents' / folder).rglob('*'):
        if path.is_file() and not path.is_symlink() and macho(path):
            queue.append((path, path))

def resolve(dep, source):
    if dep.startswith('@loader_path/'):
        return source.parent / dep[len('@loader_path/'):]
    if dep.startswith('@executable_path/'):
        return executable_dir / dep[len('@executable_path/'):]
    if dep.startswith('@rpath/'):
        suffix = dep[len('@rpath/'):]
        paths = re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.+?) \(offset',
                           output('/usr/bin/otool', '-l', str(source)))
        for raw in paths:
            base = raw.replace('@loader_path', str(source.parent)).replace('@executable_path', str(executable_dir))
            candidate = Path(base) / suffix
            if candidate.exists():
                return candidate
        candidate = Path(os.environ['KRELLIX_QT_PREFIX']) / 'lib' / suffix
        if candidate.exists():
            return candidate
        raise RuntimeError('Unresolved dependency: ' + dep + ' in ' + str(source))
    return Path(dep)

while queue:
    source, dest = queue.pop(0)
    # Read original load commands before modifying the staged copy.
    deps = [line.strip().split(' (compatibility version')[0]
            for line in output('/usr/bin/otool', '-L', str(source)).splitlines()[1:]]
    ids = output('/usr/bin/otool', '-D', str(source)).splitlines()[1:]
    for dep in deps:
        if dep in ids:
            continue
        if dep.startswith(('/usr/lib/', '/System/Library/')):
            continue
        resolved = resolve(dep, source).resolve()
        if resolved == source.resolve():  # dylib install name, not a dependency
            continue
        if not resolved.exists():
            raise RuntimeError('Missing dependency: ' + str(resolved))
        if '.framework/' in str(resolved):
            prefix, suffix = str(resolved).split('.framework/', 1)
            original = Path(prefix + '.framework')
            target_root = frameworks / original.name
            target = target_root / suffix
            key = str(original)
            if key not in copied:
                if target_root.exists():
                    raise RuntimeError('Framework name collision: ' + str(target_root))
                shutil.copytree(original, target_root, symlinks=True)
                copied[key] = target_root
                queue.append((resolved, target))
        else:
            target = frameworks / resolved.name
            key = str(resolved)
            if key not in copied:
                if target.exists():
                    raise RuntimeError('Library name collision: ' + str(target))
                shutil.copy2(resolved, target)
                copied[key] = target
                queue.append((resolved, target))
        dest.chmod(dest.stat().st_mode | 0o200)
        replacement = '@loader_path/' + os.path.relpath(target, dest.parent)
        subprocess.check_call(['/usr/bin/install_name_tool', '-change', dep, replacement, str(dest)])
    # Eliminate build-machine search paths; every external dependency now has
    # a relative loader path. Preserve system paths when present.
    paths = re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.+?) \(offset',
                       output('/usr/bin/otool', '-l', str(dest)))
    for path in set(paths):
        if path.startswith(('/usr/local/', '/opt/homebrew/', '/tmp/')):
            subprocess.check_call(['/usr/bin/install_name_tool', '-delete_rpath', path, str(dest)])

for path in app.rglob('*'):
    if not path.is_file() or path.is_symlink() or not macho(path):
        continue
    deps = output('/usr/bin/otool', '-L', str(path))
    # The first load entry for dylibs can still be their install ID. Rewrite
    # that ID too, then enforce no Homebrew/build-machine linkage remains.
    if len(output('/usr/bin/otool', '-D', str(path)).splitlines()) > 1:
        subprocess.check_call(['/usr/bin/install_name_tool', '-id', '@rpath/' + os.path.relpath(path, frameworks), str(path)])
        deps = output('/usr/bin/otool', '-L', str(path))
    load_paths = '\n'.join(deps.splitlines()[1:])
    if any(x in load_paths for x in ('/usr/local/', '/opt/homebrew/', '/tmp/')):
        raise RuntimeError('External dependency remains: ' + str(path))
print('Bundled and verified runtime dependencies:', len(copied))
