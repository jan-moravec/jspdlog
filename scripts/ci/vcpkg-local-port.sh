#!/usr/bin/env bash
# Writes a copy of the vcpkg port that builds a checkout instead of downloading a tag, to test the port before the tag
# exists. Install it with: vcpkg install jspdlog --overlay-ports=<out-dir>
#
# Usage: vcpkg-local-port.sh <checkout> <out-dir>
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <checkout> <out-dir>" >&2
    exit 2
fi

src="$(cd "$1" && pwd)"
out="$2/jspdlog"
port="$src/packaging/vcpkg/ports/jspdlog"

# vcpkg keys its binary cache on the files of the port, which do not change with the sources. A digest of what the port
# installs from the checkout, written into the portfile, stops vcpkg from restoring a package of older sources.
digest="$(
    cd "$src"
    find CMakeLists.txt LICENSE cmake include -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum |
        sha256sum | cut -d ' ' -f 1
)"

# CMake needs C:/path on Windows, not the /c/path of Git Bash.
if command -v cygpath > /dev/null; then
    src="$(cygpath -m "$src")"
fi

mkdir -p "$out"
cp "$port/vcpkg.json" "$port/usage" "$out/"
{
    printf '# Source digest: %s\n' "$digest"
    printf 'set(SOURCE_PATH "%s")\n' "$src"
    sed '/^vcpkg_from_github(/,/^)/d' "$port/portfile.cmake"
} > "$out/portfile.cmake"
