#!/usr/bin/env bash
set -euo pipefail

archive="$1"
platform="$2"
out="${3:-$(dirname "$archive")}"

source_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
archive="$(cd "$(dirname "$archive")" && pwd)/$(basename "$archive")"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
version=$(sed -n 's/^project(walnut VERSION \([0-9][0-9.]*\).*/\1/p' "$source_dir/CMakeLists.txt" | head -n 1)
identifier="io.github.thatnewcoderjscpp.walnut"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

mkdir -p "$work/unpacked"
tar -xzf "$archive" -C "$work/unpacked"
package_dir="$(find "$work/unpacked" -mindepth 1 -maxdepth 1 -type d | head -n 1)"
if [ -z "$package_dir" ] || [ ! -x "$package_dir/bin/walnut" ]; then
    echo "$archive doesn't hold a Walnut package folder" >&2
    exit 1
fi

root="$work/root"
mkdir -p "$root/usr/local"
cp -R "$package_dir" "$root/usr/local/walnut"
cat > "$root/usr/local/walnut/uninstall.sh" <<EOF
#!/bin/sh
set -e
if [ "\$(id -u)" != "0" ]; then
    exec sudo "\$0" "\$@"
fi
if [ "\$(readlink /usr/local/bin/walnut 2>/dev/null)" = "/usr/local/walnut/bin/walnut" ]; then
    rm -f /usr/local/bin/walnut
fi
rm -rf /usr/local/walnut
pkgutil --forget $identifier > /dev/null 2>&1 || true
echo "Walnut is uninstalled"
EOF
chmod 755 "$root/usr/local/walnut/uninstall.sh"

scripts="$work/scripts"
mkdir -p "$scripts"
cat > "$scripts/preinstall" <<'EOF'
#!/bin/sh
rm -rf /usr/local/walnut
exit 0
EOF
cat > "$scripts/postinstall" <<'EOF'
#!/bin/sh
mkdir -p /usr/local/bin
ln -sf /usr/local/walnut/bin/walnut /usr/local/bin/walnut
exit 0
EOF
chmod 755 "$scripts/preinstall" "$scripts/postinstall"

pkg="$out/Walnut-$version-$platform.pkg"
pkgbuild --root "$root" --identifier "$identifier" --version "$version" --scripts "$scripts" \
    --install-location / "$pkg"

dmg_dir="$work/dmg"
mkdir -p "$dmg_dir"
cp "$pkg" "$dmg_dir/Install Walnut.pkg"
cat > "$dmg_dir/Read Me.txt" <<EOF
Walnut $version for macOS ($platform)

Double-click "Install Walnut.pkg" to install Walnut into /usr/local/walnut.
It also puts the walnut command in /usr/local/bin, so it works in any terminal.

Walnut isn't signed by Apple, so macOS may say the installer can't be opened.
If it does, right-click (or Control-click) "Install Walnut.pkg" and choose Open,
or open System Settings, Privacy & Security, and click Open Anyway.

Walnut compiles programs with Apple's C++ compiler. After installing, run

    walnut init

in a terminal; it opens Apple's installer for the Command Line Tools if they
aren't installed yet. Then try a program:

    walnut run hello.wal

To uninstall Walnut, run

    sudo /usr/local/walnut/uninstall.sh

The documentation is in /usr/local/walnut/docs, and at
https://github.com/ThatNewCoderJSCpp/Walnut
EOF

dmg="$out/Walnut-$version-$platform.dmg"
rm -f "$dmg"
hdiutil create -volname "Walnut $version" -srcfolder "$dmg_dir" -ov -format UDZO "$dmg"
ls -l "$pkg" "$dmg"
