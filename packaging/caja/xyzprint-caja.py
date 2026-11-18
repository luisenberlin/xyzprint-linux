#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# xyzprint-caja.py — extensión de Caja: "Imprimir en la da Vinci 1.0 Pro".
#
# Añade una entrada al menú contextual de los ficheros G-code y .3w que llama a
# xyzsend (empaqueta a .3w y sube a la impresora). No reimplementa nada del
# protocolo: todo el trabajo lo hace la CLI.
#
# Instalación (sin root):
#   ./tools/install-caja.sh
# o a mano:
#   cp packaging/caja/xyzprint-caja.py ~/.local/share/caja-python/extensions/
#   caja -q   # y volver a abrir Caja
#
# Requiere python3-caja (paquete de Ubuntu/Mate) y xyzsend instalado
# (./tools/install-local.sh).

import os
import shlex
import shutil
import subprocess
import urllib.parse

import gi

gi.require_version('Caja', '2.0')
gi.require_version('Gtk', '3.0')

from gi.repository import Caja, GObject  # noqa: E402

EXTENSIONES = ('.gcode', '.gco', '.nc', '.g', '.3w')
ETIQUETA = 'Imprimir en la da Vinci 1.0 Pro'


def ruta_local(fichero):
    """Devuelve la ruta local de un Caja.FileInfo, o None si no es local."""
    uri = fichero.get_uri()
    if not uri or not uri.startswith('file:'):
        return None
    return urllib.parse.unquote(urllib.parse.urlparse(uri).path)


def buscar_xyzsend():
    candidatos = [
        os.path.expanduser('~/.local/bin/xyzsend'),
        os.path.expanduser('~/.local/opt/xyzprint/bin/xyzsend'),
        '/usr/local/bin/xyzsend',
        '/usr/bin/xyzsend',
    ]
    for ruta in candidatos:
        if os.access(ruta, os.X_OK):
            return ruta
    return shutil.which('xyzsend')


def buscar_terminal():
    for nombre in ('mate-terminal', 'gnome-terminal', 'xfce4-terminal',
                   'konsole', 'xterm', 'x-terminal-emulator'):
        ruta = shutil.which(nombre)
        if ruta:
            return ruta
    return None


def resumen(ruta):
    """Tamaño y tiempo estimado, leyendo solo las primeras líneas."""
    try:
        tam = os.path.getsize(ruta)
    except OSError:
        return ''

    segundos = 0
    try:
        with open(ruta, 'rb') as f:
            cabecera = f.read(4096).decode('utf-8', 'replace')
        for linea in cabecera.splitlines():
            if linea.startswith(';TIME:'):
                segundos = int(''.join(c for c in linea[6:] if c.isdigit()) or 0)
                break
            if linea.startswith('; print_time ='):
                segundos = int(''.join(c for c in linea.split('=', 1)[1]
                                       if c.isdigit()) or 0)
                break
    except (OSError, ValueError):
        pass

    texto = '{} — {:.1f} MB'.format(os.path.basename(ruta), tam / 1e6)
    if segundos > 0:
        texto += ' — ~{} h {} min'.format(segundos // 3600, (segundos % 3600) // 60)
    return texto


def confirmar(ruta):
    """Pregunta con zenity. Devuelve True/False, o None si no hay zenity."""
    zenity = shutil.which('zenity')
    if not zenity:
        return None
    detalle = resumen(ruta)
    try:
        resultado = subprocess.run(
            [zenity, '--question',
             '--title=Imprimir en la da Vinci 1.0 Pro',
             '--icon=printer',
             '--ok-label=Imprimir', '--cancel-label=Cancelar',
             '--text=¿Enviar a la impresora?\n\n{}\n\nLa impresión arranca al '
             'terminar la subida (~4-5 min).'.format(detalle)],
            timeout=300)
        return resultado.returncode == 0
    except (OSError, subprocess.SubprocessError):
        return None


def lanzar_en_terminal(terminal, xyzsend, ruta):
    """Abre una terminal con xyzsend para ver el progreso."""
    interna = '{} -- {}'.format(shlex.quote(xyzsend), shlex.quote(ruta))
    guion = (interna + '; estado=$?; echo; '
             'if [ "$estado" -eq 0 ]; then echo "Impresión lanzada."; '
             'else echo "Falló (código $estado)."; fi; '
             'read -r -p "Pulsa Intro para cerrar..." _')
    base = os.path.basename(terminal)
    if base in ('mate-terminal', 'gnome-terminal'):
        orden = [terminal, '--', 'bash', '-c', guion]
    elif base == 'x-terminal-emulator':
        # El wrapper de Debian no acepta "--": hay que pasar un único string.
        orden = [terminal, '-e', 'bash -c ' + shlex.quote(guion)]
    else:
        orden = [terminal, '-e', 'bash', '-c', guion]
    subprocess.Popen(orden, start_new_session=True)


def lanzar_sin_terminal(xyzsend, ruta):
    """Sin terminal: imprime en segundo plano y avisa con notify-send."""
    orden = ['bash', '-c',
             'estado=0; "$1" --yes -- "$2" >/dev/null 2>&1 || estado=$?; '
             'if [ "$estado" -eq 0 ]; then '
             'notify-send -i printer "da Vinci" "Impresión lanzada: $(basename "$2")"; '
             'else '
             'notify-send -i dialog-error "da Vinci" "No se pudo imprimir $(basename "$2")"; '
             'fi',
             'bash', xyzsend, ruta]
    subprocess.Popen(orden, start_new_session=True)


class XYZPrintMenu(GObject.GObject, Caja.MenuProvider):

    def __init__(self):
        GObject.GObject.__init__(self)

    def get_file_items(self, *args):
        # Caja 1.26 pasa (window, files); versiones antiguas solo (files).
        ficheros = args[-1]
        if len(ficheros) != 1:
            return None

        fichero = ficheros[0]
        if fichero.is_directory():
            return None
        if not ruta_local(fichero):
            return None
        if not fichero.get_name().lower().endswith(EXTENSIONES):
            return None
        if not buscar_xyzsend():
            return None

        item = Caja.MenuItem(name='XYZPrint::imprimir',
                             label=ETIQUETA,
                             icon='document-print')
        item.connect('activate', self.on_activar, ficheros)
        return [item]

    def on_activar(self, _item, ficheros):
        ruta = ruta_local(ficheros[0])
        if not ruta:
            return

        xyzsend = buscar_xyzsend()
        if not xyzsend:
            subprocess.Popen(['zenity', '--error', '--title=da Vinci',
                              '--text=No encuentro xyzsend. Instala con '
                              './tools/install-local.sh'])
            return

        respuesta = confirmar(ruta)
        if respuesta is False:
            return

        terminal = buscar_terminal()
        if terminal:
            lanzar_en_terminal(terminal, xyzsend, ruta)
        else:
            lanzar_sin_terminal(xyzsend, ruta)
