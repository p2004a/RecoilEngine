#!/bin/bash

set -e -u -o pipefail

cd /build/src

package_suffix="${1-}"
branch="$(git rev-parse --abbrev-ref HEAD)"
tag_name="spring_bar_{$branch}$(git describe --abbrev=7)"
bin_name="${tag_name}_${ENGINE_PLATFORM}${package_suffix}-minimal-portable.7z"
dbg_name="${tag_name}_${ENGINE_PLATFORM}${package_suffix}-minimal-symbols.tar.zst"

cd /build/out/install

# Compute md5 hashes of all files in archive. We additionally gzip it as gzip adds
# checksum to the list itself. To validate just `zcat files.md5.gz | md5sum -c -`
find . -type f ! -name '*.dbg' ! -name files.md5.gz -exec md5sum {} \; | gzip > files.md5.gz

rm -f "/build/artifacts/$bin_name" "/build/artifacts/$dbg_name"

# Trigger compression of main binaries and debug info concurrently
7z a -t7z -m0=lzma -mx=9 -mfb=64 -md=32m -ms=on "/build/artifacts/$bin_name" ./* -xr\!*.dbg &

DEBUG_SYMBOLS=$(find ./ -name '*.dbg')
if [[ -n $DEBUG_SYMBOLS ]]; then
    tar cvf - $DEBUG_SYMBOLS | zstd -T0 > "/build/artifacts/$dbg_name" &
fi

wait
