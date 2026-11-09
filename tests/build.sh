#!/bin/sh
# Builds gl2d9's test programs (32-bit, mingw-w64) into tests/bin/.
#   GLIDE_SRC=/path/to/glide tests/build.sh
# GLIDE_SRC is 3dfx's open-sourced Glide tree (glide2x/ and glide3x/ under api/), for the
# headers only. The import libraries are made from gl2d9's .def files (the names the games
# import: _grFoo@N), into tests/bin/.
set -e
: "${GLIDE_SRC:?set GLIDE_SRC to the 3dfx Glide source tree}"
here=$(cd "$(dirname "$0")" && pwd)
src="$here/../src"
mkdir -p "$here/bin"
CC=${CC:-i686-w64-mingw32-gcc}
DLLTOOL=${DLLTOOL:-i686-w64-mingw32-dlltool}
for d in glide2x glide3x; do
  { echo "LIBRARY $d.dll"; echo "EXPORTS"; sed -n 's/^ *_\([^ ;]*\) *=.*/\1 == _\1/p' "$src/$d.def"; } > "$here/bin/imp-$d.def"
  $DLLTOOL -d "$here/bin/imp-$d.def" -l "$here/bin/imp-$d.a"
done
G2="-I$GLIDE_SRC/api/glide2x/cvg/glide/src -I$GLIDE_SRC/api/glide2x/cvg/incsrc -I$GLIDE_SRC/api/swlibs/fxmisc -I$GLIDE_SRC/api/glide2x/cvg/init"
G3="-I$GLIDE_SRC/api/glide3x/cvg/glide3/src -I$GLIDE_SRC/api/glide3x/cvg/incsrc -I$GLIDE_SRC/api/swlibs/fxmisc -I$GLIDE_SRC/api/glide3x/cvg/init"
for f in "$here"/glide2/*.c; do
  t=$(basename "$f" .c)
  $CC -O1 -w -D__WIN32__ -DGLIDE_HW_CVG -DCVG -include "$here/glide2/pre.h" $G2 -o "$here/bin/$t.exe" "$f" \
    "$here/bin/imp-glide2x.a" -luser32 -lgdi32
done
for f in "$here"/glide3/*.c; do
  t=$(basename "$f" .c)
  $CC -O1 -w -D__WIN32__ -DGLIDE3 -DGLIDE3_ALPHA $G3 -o "$here/bin/$t.exe" "$f" "$here/bin/imp-glide3x.a" -luser32 -lgdi32
done
echo "built: $(cd "$here/bin" && ls *.exe | tr '\n' ' ')"
