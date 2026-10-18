#!/usr/bin/env bash
# Fork MT6765Pkg -> MT6768Pkg: copy tree, rename MT6765->MT6768 in names+contents.
set -euo pipefail

SRC=/home/pencilqwq/ed2k_mt6768/refs/edk2-mt6765
DST=/home/pencilqwq/ed2k_mt6768/ws

echo "=== copy port pieces ==="
cp -r "$SRC/MT6765Pkg" "$DST/MT6768Pkg"
cp -r "$SRC/BootShim" "$DST/BootShim"
mkdir -p "$DST/blobs"
cp -p "$SRC"/blobs/* "$DST/blobs/" 2>/dev/null || true
cp -p "$SRC"/build.sh "$SRC"/build_common.sh "$SRC"/build_bootshim.sh "$SRC"/firstrun.sh "$SRC"/.gitignore "$DST/"

PKG="$DST/MT6768Pkg"

echo "=== rename files & dirs containing MT6765 (depth-first) ==="
# depth-first so we rename children before parents
while IFS= read -r -d '' f; do
  d=$(dirname "$f"); b=$(basename "$f")
  nb=${b//MT6765/MT6768}
  if [ "$b" != "$nb" ]; then
    mv "$f" "$d/$nb"
  fi
done < <(find "$PKG" -depth -name '*MT6765*' -print0)

# also rename the device dsc roo -> lancelot
if [ -f "$PKG/Devices/roo.dsc" ]; then
  mv "$PKG/Devices/roo.dsc" "$PKG/Devices/lancelot.dsc"
fi

echo "=== sed MT6765->MT6768 in all contents ==="
find "$PKG" -type f \( -name '*.c' -o -name '*.h' -o -name '*.S' -o -name '*.inf' \
   -o -name '*.dsc' -o -name '*.dec' -o -name '*.fdf' -o -name '*.inc' \
   -o -name '*.asl' -o -name '*.aslc' -o -name '*.uni' -o -name '*.idf' \) -print0 \
   | xargs -0 sed -i 's/MT6765/MT6768/g'

echo "=== sed roo.dsc -> lancelot.dsc in build scripts ==="
sed -i 's/roo\.dsc/lancelot.dsc/g' "$DST"/build.sh

echo "=== verify ==="
echo "MT6768Pkg top:"; ls "$PKG"
echo; echo "Devices:"; ls "$PKG/Devices"
echo; echo "Library:"; ls "$PKG/Library"
echo; echo "Drivers:"; ls "$PKG/Drivers"
echo; echo "remaining MT6765 refs in contents:"; grep -rl 'MT6765' "$PKG" 2>/dev/null | head || echo "(none)"
echo "done."