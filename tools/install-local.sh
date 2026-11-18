#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# install-local.sh — instala xyzprint-cli y xyzsend en ~/.local sin root.
#
#   ./tools/install-local.sh                  # instala en ~/.local/opt/xyzprint
#   ./tools/install-local.sh --prefix /opt/xyzprint
#   ./tools/install-local.sh --bundle         # copia también las librerías
#   ./tools/install-local.sh --uninstall
#
# Deja symlinks en ~/.local/bin, así que 'xyzsend' funciona desde cualquier
# directorio (si ~/.local/bin está en el PATH).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX="${HOME}/.local/opt/xyzprint"
BINDIR="${HOME}/.local/bin"
BUNDLE=0
UNINSTALL=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix)    PREFIX="$2"; shift 2 ;;
        --bindir)    BINDIR="$2"; shift 2 ;;
        --bundle)    BUNDLE=1; shift ;;
        --uninstall) UNINSTALL=1; shift ;;
        -h|--help)
            sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "error: opción desconocida: $1" >&2; exit 2 ;;
    esac
done

if [[ "$UNINSTALL" -eq 1 ]]; then
    rm -f "$BINDIR/xyzsend" "$BINDIR/xyzprint-cli"
    rm -rf "$PREFIX"
    echo "desinstalado: $PREFIX y los enlaces de $BINDIR"
    exit 0
fi

# Sirve tanto para el árbol de código (build/) como para un paquete ya
# compilado (bin/), que es lo que genera tools/make-dist.sh.
CLI=""
for candidato in "$ROOT/build/xyzprint-cli" "$ROOT/bin/xyzprint-cli"; do
    if [[ -x "$candidato" ]]; then
        CLI="$candidato"
        break
    fi
done
if [[ -z "$CLI" ]]; then
    cat >&2 <<EOF
error: no encuentro xyzprint-cli en $ROOT/build ni en $ROOT/bin
  si es el código fuente, compila primero:
    cmake -S "$ROOT/src" -B "$ROOT/build" -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build "$ROOT/build"
EOF
    exit 1
fi

# Perfiles: profiles/ en el código, share/xyzprint/profiles/ en el paquete.
PERFILES=""
for candidato in "$ROOT"/profiles/*.json \
                 "$ROOT"/share/xyzprint/profiles/*.json; do
    if [[ -f "$candidato" ]]; then
        PERFILES="$candidato"
        break
    fi
done
if [[ -z "$PERFILES" ]]; then
    echo "error: no encuentro el perfil davinci-1.0-pro-pla.json en $ROOT" >&2
    exit 1
fi

echo "instalando en $PREFIX"
XYZPRINT_SEND=""
for candidato in "$ROOT/tools/xyzsend" "$ROOT/bin/xyzsend"; do
    if [[ -f "$candidato" ]]; then
        XYZPRINT_SEND="$candidato"
        break
    fi
done
if [[ -z "$XYZPRINT_SEND" ]]; then
    echo "error: no encuentro xyzsend en $ROOT" >&2
    exit 1
fi

install -d "$PREFIX/bin" "$PREFIX/share/xyzprint/profiles" "$PREFIX/lib" "$BINDIR"
install -m 0755 "$CLI" "$PREFIX/bin/xyzprint-cli"
install -m 0755 "$XYZPRINT_SEND" "$PREFIX/bin/xyzsend"
install -m 0644 "$ROOT"/profiles/*.json "$PREFIX/share/xyzprint/profiles/" 2>/dev/null \
    || install -m 0644 "$ROOT"/share/xyzprint/profiles/*.json "$PREFIX/share/xyzprint/profiles/"

if [[ "$BUNDLE" -eq 1 ]]; then
    echo "copiando librerías compartidas (para llevar a otro equipo)"
    # Se copia todo lo que no sea parte del sistema base: así el binario
    # funciona en un equipo con Qt6 más antiguo o sin él. Misma arquitectura
    # (x86_64) y glibc >= la de este equipo.
    ldd "$CLI" | awk '/=> \//{print $3} /^\//{print $1}' | sort -u | while read -r lib; do
        case "$(basename "$lib")" in
            libc.so.*|libm.so.*|libdl.so.*|libpthread.so.*|librt.so.*|libresolv.so.*|ld-linux*)
                continue ;;
        esac
        install -m 0644 "$lib" "$PREFIX/lib/"
    done
    echo "  $(find "$PREFIX/lib" -name '*.so*' | wc -l) librerías copiadas"
fi

ln -sfn "$PREFIX/bin/xyzprint-cli" "$BINDIR/xyzprint-cli"
ln -sfn "$PREFIX/bin/xyzsend" "$BINDIR/xyzsend"

echo
echo "instalado:"
echo "  $PREFIX/bin/xyzprint-cli"
echo "  $PREFIX/bin/xyzsend"
echo "  $PREFIX/share/xyzprint/profiles/"
echo "  $BINDIR/xyzsend -> $PREFIX/bin/xyzsend"

case ":$PATH:" in
    *":$BINDIR:"*) ;;
    *)
        echo
        echo "AVISO: $BINDIR no está en el PATH. Añádelo a ~/.profile:"
        echo "  export PATH=\"$BINDIR:\$PATH\""
        ;;
esac

# ------------------------------------------------------------ comprobar entorno
echo
echo "comprobando dependencias:"
ok=1
if command -v CuraEngine >/dev/null 2>&1; then
    echo "  CuraEngine      : $(command -v CuraEngine)"
else
    echo "  CuraEngine      : FALTA (sudo apt install cura-engine) — solo hace falta para 'slice'"
    ok=0
fi
if [[ -f /usr/share/cura/resources/definitions/fdmprinter.def.json ]]; then
    echo "  definiciones    : /usr/share/cura/resources/definitions"
else
    echo "  definiciones    : FALTAN (sudo apt install cura) — solo para 'slice'"
    ok=0
fi
if [[ -e /dev/ttyACM0 ]]; then
    if [[ -r /dev/ttyACM0 && -w /dev/ttyACM0 ]]; then
        echo "  impresora       : /dev/ttyACM0 accesible"
    else
        echo "  impresora       : /dev/ttyACM0 SIN permisos (sudo usermod -aG dialout \$USER)"
        ok=0
    fi
else
    echo "  impresora       : no hay /dev/ttyACM0 (¿encendida y conectada?)"
    ok=0
fi
[[ "$ok" -eq 0 ]] && echo "  (los avisos de arriba no impiden usar un .3w ya hecho)"

echo
echo "prueba:  xyzsend --dry-run ~/ruta/pieza.gcode"
