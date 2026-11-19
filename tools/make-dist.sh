#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# make-dist.sh — empaqueta xyzprint-cli para distribuirlo a otro equipo Linux.
#
#   ./tools/make-dist.sh                  # binario + perfiles + scripts + docs
#   ./tools/make-dist.sh --with-libs      # además las librerías Qt6/OpenSSL
#   ./tools/make-dist.sh --with-source    # además el código fuente
#
# Genera en dist/:
#   xyzprint-<versión>-linux-x86_64/         árbol listo para usar
#   xyzprint-<versión>-linux-x86_64.tar.gz   paquete
#   xyzprint-<versión>-linux-x86_64.tar.gz.sha256
#
# En el equipo destino:  tar xf ... && cd ... && ./tools/install-local.sh

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WITH_LIBS=0
WITH_SOURCE=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --with-libs)   WITH_LIBS=1; shift ;;
        --with-source) WITH_SOURCE=1; shift ;;
        -h|--help)
            sed -n '2,18p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "error: opción desconocida: $1" >&2; exit 2 ;;
    esac
done

VERSION="$(sed -n 's/^ *VERSION \([0-9][0-9.]*\).*/\1/p' "$ROOT/src/CMakeLists.txt" | head -1)"
VERSION="${VERSION:-0.1.0}"
ARCH="$(uname -m)"
NOMBRE="xyzprint-${VERSION}-linux-${ARCH}"
STAGE="$ROOT/dist/$NOMBRE"

echo "== compilando =="
cmake -S "$ROOT/src" -B "$ROOT/build" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$ROOT/build" >/dev/null
ctest --test-dir "$ROOT/build" --output-on-failure >/dev/null
echo "   tests: OK"

echo "== preparando $STAGE =="
rm -rf "$STAGE" "$ROOT/dist/$NOMBRE.tar.gz" "$ROOT/dist/$NOMBRE.tar.gz.sha256"
install -d "$STAGE/bin" "$STAGE/tools" "$STAGE/share/xyzprint/profiles" \
           "$STAGE/packaging/caja" "$STAGE/packaging/udev" "$STAGE/docs"

install -m 0755 "$ROOT/build/xyzprint-cli" "$STAGE/bin/xyzprint-cli"
install -m 0755 "$ROOT/tools/xyzsend" "$STAGE/bin/xyzsend"
install -m 0755 "$ROOT/tools/install-local.sh" "$STAGE/tools/install-local.sh"
install -m 0755 "$ROOT/tools/install-caja.sh" "$STAGE/tools/install-caja.sh"
install -m 0644 "$ROOT"/profiles/*.json "$STAGE/share/xyzprint/profiles/"
install -m 0644 "$ROOT/packaging/caja/xyzprint-caja.py" "$STAGE/packaging/caja/"
install -m 0644 "$ROOT"/packaging/udev/*.rules "$STAGE/packaging/udev/"
install -m 0644 "$ROOT"/docs/*.md "$STAGE/docs/"
[[ -f "$ROOT/LICENSE" ]] && install -m 0644 "$ROOT/LICENSE" "$STAGE/LICENSE"
[[ -f "$ROOT/README.md" ]] && install -m 0644 "$ROOT/README.md" "$STAGE/README.md"

if [[ "$WITH_SOURCE" -eq 1 ]]; then
    install -d "$STAGE/src" "$STAGE/docs"
    cp -a "$ROOT/src/." "$STAGE/src/"
    cp -a "$ROOT/docs/." "$STAGE/docs/"
    install -m 0644 "$ROOT/README.md" "$STAGE/README.md"
    install -m 0644 "$ROOT/LICENSE" "$STAGE/LICENSE"
fi

if [[ "$WITH_LIBS" -eq 1 ]]; then
    echo "== copiando librerías compartidas =="
    install -d "$STAGE/lib"
    ldd "$ROOT/build/xyzprint-cli" | awk '/=> \//{print $3} /^\//{print $1}' \
        | sort -u | while read -r lib; do
        case "$(basename "$lib")" in
            libc.so.*|libm.so.*|libdl.so.*|libpthread.so.*|librt.so.*|libresolv.so.*|ld-linux*)
                continue ;;
        esac
        install -m 0644 "$lib" "$STAGE/lib/"
    done
    echo "   $(find "$STAGE/lib" -name '*.so*' | wc -l) librerías"
    cat > "$STAGE/lib/LEEME.txt" <<'EOF'
Copias de las librerías que necesita xyzprint-cli (Qt6 Core/SerialPort, OpenSSL,
ICU, glib, …). xyzsend y xyzprint-cli las cargan solos: no hay que configurar
LD_LIBRARY_PATH.

Requisito: misma arquitectura y una glibc igual o más nueva que la del equipo
donde se generó este paquete.
EOF
fi

echo "== comprimiendo =="
tar -C "$ROOT/dist" -czf "$ROOT/dist/$NOMBRE.tar.gz" "$NOMBRE"
( cd "$ROOT/dist" && sha256sum "$NOMBRE.tar.gz" > "$NOMBRE.tar.gz.sha256" )

echo
echo "listo:"
du -sh "$ROOT/dist/$NOMBRE.tar.gz" | sed 's/^/  /'
echo "  $ROOT/dist/$NOMBRE.tar.gz"
cat "$ROOT/dist/$NOMBRE.tar.gz.sha256" | sed 's/^/  sha256: /'
echo
echo "instalación en el equipo destino:"
echo "  tar xf $NOMBRE.tar.gz"
echo "  cd $NOMBRE"
echo "  ./tools/install-local.sh"
