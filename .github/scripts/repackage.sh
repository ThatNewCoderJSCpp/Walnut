#!/usr/bin/env bash
set -euo pipefail

archive="$1"
platform="$2"
generators="$3"
suffix="${4:-}"
out="${5:-$(dirname "$archive")}"

source_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
archive="$(cd "$(dirname "$archive")" && pwd)/$(basename "$archive")"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
version=$(sed -n 's/^project(walnut VERSION \([0-9][0-9.]*\).*/\1/p' "$source_dir/CMakeLists.txt" | head -n 1)

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

mkdir -p "$work/unpacked"
case "$archive" in
    *.zip) unzip -q "$archive" -d "$work/unpacked" ;;
    *) tar -xzf "$archive" -C "$work/unpacked" ;;
esac
root="$(find "$work/unpacked" -mindepth 1 -maxdepth 1 -type d | head -n 1)"
if [ -z "$root" ] || [ ! -d "$root/bin" ]; then
    echo "$archive doesn't hold a Walnut package folder" >&2
    exit 1
fi

cmake -S "$source_dir/Packaging" -B "$work/pkg" -DWALNUT_STAGE="$root" -DWALNUT_VERSION="$version" \
    -DWALNUT_PLATFORM="$platform" -DWALNUT_PACKAGE_SUFFIX="$suffix" "-DWALNUT_GENERATORS=$generators"
(cd "$work/pkg" && cpack)

find "$work/pkg" -maxdepth 1 -type f -name "Walnut-$version-$platform$suffix.*" -exec mv {} "$out/" \;
ls -l "$out"
