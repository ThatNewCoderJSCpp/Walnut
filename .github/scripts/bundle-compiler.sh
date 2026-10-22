#!/usr/bin/env bash
set -euo pipefail

package="$1"
toolchain_zip="$2"
toolchain_url="$3"

package="$(cd "$(dirname "$package")" && pwd)/$(basename "$package")"
toolchain_zip="$(cd "$(dirname "$toolchain_zip")" && pwd)/$(basename "$toolchain_zip")"
name="$(basename "$toolchain_zip" .zip)"
stem="$(basename "$package" .zip)"
out="$(dirname "$package")/$stem-with-compiler.zip"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

unzip -q "$package" -d "$work"
root="$(find "$work" -mindepth 1 -maxdepth 1 -type d | head -n 1)"
mkdir -p "$root/toolchain"
unzip -q "$toolchain_zip" -d "$root/toolchain"
if [ ! -f "$root/toolchain/$name/bin/clang++.exe" ]; then
    echo "the toolchain zip doesn't contain $name/bin/clang++.exe" >&2
    exit 1
fi
printf '%s\n%s\n%s\n' "$name" "$(sha256sum "$toolchain_zip" | cut -d' ' -f1)" "$toolchain_url" > "$root/toolchain/walnut-toolchain.txt"

mv "$root" "$work/$stem-with-compiler"
rm -f "$out"
(cd "$work" && zip -qr -9 "$out" "$stem-with-compiler")
echo "$out"
