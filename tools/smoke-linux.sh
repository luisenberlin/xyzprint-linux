#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# smoke-linux.sh — comprobación de solo lectura de la da Vinci 1.0 Pro.
#
# No sube nada ni arranca la impresión: solo abre el puerto y consulta
# XYZv3/query=taginfo.
#
# Uso:
#   ./tools/smoke-linux.sh [/dev/ttyACM0]

set -euo pipefail

PORT="${1:-/dev/ttyACM0}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLI="$ROOT/build/xyzprint-cli"

if [[ ! -e "$PORT" ]]; then
    echo "error: no existe $PORT" >&2
    echo "pistas:" >&2
    echo "  - ¿está la impresora encendida y conectada por USB?" >&2
    echo "  - mira 'ls /dev/ttyACM*' y 'dmesg | tail'" >&2
    exit 1
fi

if [[ ! -r "$PORT" || ! -w "$PORT" ]]; then
    echo "error: sin permisos de lectura/escritura sobre $PORT" >&2
    echo "instala packaging/udev/99-xyzprinting.rules o añade tu usuario al grupo del dispositivo" >&2
    exit 1
fi

if [[ ! -x "$CLI" ]]; then
    echo "error: no existe $CLI; compila con:" >&2
    echo "  cmake -S src -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build" >&2
    exit 1
fi

echo "== puertos detectados =="
"$CLI" ports || true

echo
echo "== estado de $PORT (solo lectura) =="
"$CLI" status --port "$PORT"
