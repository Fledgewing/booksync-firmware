"""Stamp BookSync identity and immutable source separately from CrossInk labels."""
import os
from pathlib import Path
import subprocess
import sys

Import('env')
root = Path(env['PROJECT_DIR'])
sys.path.insert(0, str(root / 'tools/version'))
from identity import BASE, BASE_COMMIT, identity, artifact_name

manifest = os.environ.get('BOOKSYNC_VERSION_FILE')
product, tag = identity(Path(manifest) if manifest else None,
                        os.environ.get('BOOKSYNC_RELEASE_TAG', ''),
                        os.environ.get('BOOKSYNC_TEST_RUN', '1'))
commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
if len(commit) != 40 or any(c not in '0123456789abcdef' for c in commit):
    raise ValueError('invalid source commit')
scoped = [('BOOKSYNC_VERSION', product), ('BOOKSYNC_CROSSINK_BASE', BASE),
          ('BOOKSYNC_CROSSINK_COMMIT', BASE_COMMIT), ('BOOKSYNC_SOURCE_COMMIT', commit), ('BOOKSYNC_RELEASE_TAG', tag)]

def stamp(node_env, node):
    build = node_env.Clone()
    build.Append(CPPDEFINES=[(k, '\\\"' + v + '\\\"') for k, v in scoped])
    return build.Object(node)

env.AddBuildMiddleware(stamp, '*src/booksync/system/Version.cpp')
# Xtensa ignores retain; force the dormant F31 budget root into hardware links.
# LINKFLAGS avoids putting a linker-only change in every compile command.
if 'simulator' not in env['PIOENV']:
    env.Append(LINKFLAGS=['-Wl,--undefined=booksync_update_pick_link'])
if tag:
    # Artifact copying only. This hook has no release/feed publishing capability.
    import shutil
    def package(source, target, env):
        src = Path(str(target[0]))
        shutil.copyfile(src, src.parent / artifact_name(env['PIOENV'], tag))
    env.AddPostAction('$BUILD_DIR/${PROGNAME}.bin', package)
