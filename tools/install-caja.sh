#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# install-caja.sh — añade "Imprimir en la da Vinci 1.0 Pro" al menú contextual
# de Caja (MATE) para ficheros .gcode y .3w.
#
#   ./tools/install-caja.sh              # instala (sin root)
#   ./tools/install-caja.sh --uninstall  # lo quita
#
# Requiere: python3-caja (extensión Python de Caja) y xyzsend (install-local.sh).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DESTINO="${XDG_DATA_HOME:-$HOME/.local/share}/caja-python/extensions"
ORIGEN="$ROOT/packaging/caja/xyzprint-caja.py"
FICHERO="$DESTINO/xyzprint-caja.py"

UNINSTALL=0
[[ "${1:-}" == "--uninstall" ]] && UNINSTALL=1

if [[ "$UNINSTALL" -eq 1 ]]; then
    rm -f "$FICHERO"
    echo "quitado: $FICHERO"
    echo "reinicia Caja para que desaparezca del menú:  caja -q"
    exit 0
fi

# ------------------------------------------------------------- comprobaciones
if [[ ! -f "$ORIGEN" ]]; then
    echo "error: falta $ORIGEN" >&2
    exit 1
fi

if ! python3 - <<'PY' >/dev/null 2>&1
import gi
gi.require_version('Caja', '2.0')
from gi.repository import Caja
PY
then
    cat >&2 <<'EOF'
error: falta la extensión Python de Caja (python3-caja).
  sudo apt install python3-caja
EOF
    exit 1
fi

if ! command -v xyzsend >/dev/null 2>&1 \
   && [[ ! -x "$HOME/.local/opt/xyzprint/bin/xyzsend" ]]; then
    echo "aviso: no encuentro xyzsend; instálalo antes con ./tools/install-local.sh" >&2
fi

if [[ ! -d "$DESTINO" ]]; then
    mkdir -p "$DESTINO"
fi

install -m 0644 "$ORIGEN" "$FICHERO"
echo "instalado: $FICHERO"

# --------------------------------------------------------------- recargar Caja
if pgrep -x caja >/dev/null 2>&1; then
    echo "reiniciando Caja para cargar la extensión..."
    caja -q >/dev/null 2>&1 || true
    sleep 1
    # Reabre el gestor en segundo plano si hay sesión gráfica.
    if [[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
        nohup caja --no-desktop >/dev/null 2>&1 &
        disown || true
    fi
    echo "hecho. Abre una carpeta y prueba con un .gcode: botón derecho →"
    echo "  \"Imprimir en la da Vinci 1.0 Pro\""
else
    echo "Caja no está en ejecución; la extensión se cargará al abrirlo."
fi

echo
echo "prueba sin imprimir:  xyzsend --dry-run /ruta/pieza.gcode"
echo "desinstalar:          ./tools/install-caja.sh --uninstall"
