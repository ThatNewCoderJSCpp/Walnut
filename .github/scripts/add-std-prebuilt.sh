#!/usr/bin/env bash
set -euo pipefail

package="$1"
prebuilt="$2"

package="$(cd "$(dirname "$package")" && pwd)/$(basename "$package")"
prebuilt="$(cd "$prebuilt" && pwd)"

if ! find "$prebuilt" -name objects.txt | grep -q .; then
    echo "$prebuilt doesn't hold any precompiled standard library modules" >&2
    exit 1
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

unzip -q "$package" -d "$work"
root="$(find "$work" -mindepth 1 -maxdepth 1 -type d | head -n 1)"
if [ -z "$root" ] || [ ! -d "$root/bin" ]; then
    echo "$package doesn't hold a Walnut package folder" >&2
    exit 1
fi

rm -rf "$root/std-prebuilt"
cp -r "$prebuilt" "$root/std-prebuilt"
echo "$(find "$root/std-prebuilt" -name objects.txt | wc -l) precompiled modules added to $(basename "$package")"

rm -f "$package"
(cd "$work" && zip -qr -9 "$package" "$(basename "$root")")
echo "$package"
