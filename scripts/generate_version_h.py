# scripts/generate_version_h.py
# Generates version.h from version.h.in with build-time timestamp.
# Usage: python scripts/generate_version_h.py <in_file> <out_file> <build_type> <major> <minor> <patch> <git_tag> <git_commit> <git_dirty>
import sys
import time

# CMake VERBATIM silently drops empty arguments, so optional trailing args
# (git_commit is "" when the tree sits exactly on a tag) may be missing.
def _arg(index, default=""):
    return sys.argv[index] if len(sys.argv) > index else default

infile = sys.argv[1]
outfile = sys.argv[2]
build_type = sys.argv[3]
version_major = sys.argv[4]
version_minor = sys.argv[5]
version_patch = sys.argv[6]
git_tag = sys.argv[7]
git_commit = _arg(8)
git_dirty = _arg(9, "0")

with open(infile, 'r', encoding='utf-8') as f:
    content = f.read()

content = content.replace('@BUILD_TIMESTAMP@', time.strftime('%Y-%m-%d %H:%M:%S'))
content = content.replace('@VERSION_MAJOR@', version_major)
content = content.replace('@VERSION_MINOR@', version_minor)
content = content.replace('@VERSION_PATCH@', version_patch)
content = content.replace('@VERSION_TAG@', git_tag)
content = content.replace('@VERSION_COMMIT@', git_commit)
content = content.replace('@VERSION_DIRTY@', git_dirty)
content = content.replace('@CMAKE_BUILD_TYPE@', build_type)

with open(outfile, 'w', encoding='utf-8', newline='') as f:
    f.write(content)

print(f'[version.h] Generated: {outfile}')
