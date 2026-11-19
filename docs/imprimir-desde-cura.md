# Imprimir desde Cura en la da Vinci 1.0 Pro

Cura lamina; `xyzsend` empaqueta el `.3w` y lo sube. Un solo comando.

```
Cura ──(File → Save to Disk)──► pieza.gcode ──xyzsend──► pieza.3w ──USB──► da Vinci
```

No se usa el plugin "USB printing" de Cura: habla Marlin en texto plano y esta
impresora espera el protocolo `XYZv3` con el `.3w` cifrado.

## 1. Qué se instala y dónde

```
~/.local/opt/xyzprint/
├── bin/
│   ├── xyzprint-cli        CLI completa (ports, status, slice, wrap, send…)
│   └── xyzsend             atajo: empaqueta + imprime
├── lib/                    (opcional, con --bundle) librerías Qt6 y demás
└── share/xyzprint/
    └── profiles/davinci-1.0-pro-pla.json

~/.local/bin/xyzsend        → enlace simbólico
~/.local/bin/xyzprint-cli   → enlace simbólico
```

`~/.local/bin` está en el `PATH` en Ubuntu/Mate, así que `xyzsend` funciona
desde cualquier directorio.

Instalar (o reinstalar tras recompilar):

```sh
cd ~/Escritorio/xyzprint/xyzprint
cmake -S src -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./tools/install-local.sh
```

Opciones:

| Opción | Efecto |
|---|---|
| `--prefix RUTA` | instala en otra carpeta (por defecto `~/.local/opt/xyzprint`) |
| `--bundle` | copia las `.so` necesarias a `lib/` (para llevar a otro equipo) |
| `--uninstall` | borra la instalación y los enlaces |

## 2. Dependencias

Para **enviar** (lo único imprescindible): Qt6 Core + SerialPort y OpenSSL.

```sh
sudo apt install libqt6core6 libqt6serialport6 libssl3
```

Para **laminar** (`slice`) además:

```sh
sudo apt install cura-engine cura
```

- `cura-engine` → `/usr/bin/CuraEngine` (4.13 en Ubuntu 22.04)
- `cura` → definiciones en `/usr/share/cura/resources/definitions`
  (solo se usan `fdmprinter.def.json` y `fdmextruder.def.json`)

Permisos del puerto: el usuario debe estar en `dialout`.

```sh
sudo usermod -aG dialout "$USER"   # cerrar sesión y volver a entrar
```

### Copiar a otro ordenador

Dos formas:

1. **Con dependencias del sistema** (recomendado): instala los paquetes de
   arriba y copia `~/.local/opt/xyzprint/` tal cual.
2. **Portable**: `./tools/install-local.sh --bundle` y copia `~/.local/opt/xyzprint/`.
   `xyzsend` añade `lib/` a `LD_LIBRARY_PATH` solo, así que no hace falta nada
   más. Requisitos: misma arquitectura (x86_64) y glibc igual o más nueva que
   la del equipo donde se empaquetó.

El laminador (`CuraEngine` + definiciones) **no** se empaqueta: si lo necesitas
en el otro equipo, instala `cura-engine` y `cura` allí (o copia las dos
definiciones y usa `--definitions`).

## 3. Uso diario

```sh
xyzsend pieza.gcode            # pregunta confirmación y luego imprime
xyzsend -y pieza.gcode         # sin preguntar
xyzsend -y ~/Descargas/pieza.gcode
xyzsend --dry-run pieza.gcode  # solo crea el .3w, no envía nada
```

Opciones:

| Opción | Para qué |
|---|---|
| `-y`, `--yes` | no preguntar |
| `-p`, `--port` | otro puerto (por defecto `/dev/ttyACM0`) |
| `-n`, `--name` | nombre visible en la impresora |
| `-o`, `--output` | dónde dejar el `.3w` |
| `--dry-run` | empaqueta y valida el CRC, sin imprimir |

El `.3w` se deja junto al `.gcode`, así puedes reimprimirlo sin volver a
empaquetar:

```sh
xyzsend -y pieza.3w
```

Control mientras imprime:

```sh
xyzprint-cli status     # estado, temperaturas, trabajo
xyzprint-cli pause
xyzprint-cli resume
xyzprint-cli cancel
```

## 4. Configurar Cura (4.x y 5.x)

Añadir impresora: **Preferences → Printers → Add → Custom FFF printer**.

| Ajuste | Valor |
|---|---|
| Machine settings → X (Width) | 200 mm |
| Y (Depth) | 200 mm |
| Z (Height) | 200 mm |
| Origin at center | **desmarcado** |
| G-code flavor | RepRap (Marlin/Sprinter) |
| Heated bed | marcado |
| Nozzle size | 0,4 mm |
| Filament diameter | 1,75 mm |

No uses la definición "XYZprinting da Vinci 1.0 Pro" que trae Cura: su
start/end G-code no es el de esta máquina y le faltan `M108` y `M191`.

**Start G-code**:

```gcode
G21 ; set units to millimeters
M107
M108
M140 S{material_bed_temperature_layer_0}
M190 S{material_bed_temperature_layer_0}
M104 S{material_print_temperature_layer_0}
M109 S{material_print_temperature_layer_0}
M191
G90
G92 E0
M82
```

`G21 ;` es obligatorio: `encode` lo usa como marca del final de la cabecera.

**End G-code**:

```gcode
M104 S0
M140 S0
G92 E0
G1 F1800.000 E-4.00000
G92 E0
M84
```

Luego **File → Save to Disk** y `xyzsend` sobre el `.gcode`. En Cura 5.x el
bloque está en *Preferences → Printers → Manage Printers → Machine Settings*.

## 4.1. Compatibilidad entre versiones de Cura

El flujo **Cura → G-code → `xyzsend`** no depende de la versión: `xyzsend` solo
lee texto G-code. Verificado con un G-code generado por **CuraEngine 5.14.0** y
empaquetado con `xyzsend --dry-run`: marcadores `G21 ;`, `M108`, `M191` y `M82`
presentes, `.3w` con CRC-32 válido. El bloque Start/End de esta guía sirve tal
cual en Cura 5.x.

Antes de enviar cualquier G-code, sea de la versión que sea:

```sh
node tools/check-gcode.mjs pieza.gcode
```

Comprueba los marcadores que la impresora necesita e imprime el laminador y el
tiempo estimados. Si falla, corrige el Start G-code en Cura; no hay que tocar
`xyzsend`.

### `xyzprint-cli slice` con CuraEngine 5.x

El perfil `profiles/davinci-1.0-pro-pla.json` funciona con CuraEngine 4.13 y
5.14: incluye `roofing_layer_count` y `flooring_layer_count` a 0, ajustes que
5.x exige explícitamente al laminar por CLI (4.13 los toma de sus defectos).
Para usar otro laminador:

```sh
CURA_ENGINE=/ruta/a/CuraEngine \
CURA_DEFINITIONS=/ruta/a/resources/definitions \
xyzprint-cli --keep-gcode salida.gcode slice modelo.stl salida.3w
```

También están las opciones `--cura` y `--definitions`. Comprueba la versión con
`CuraEngine --version`.

**Atención con el AppImage de Cura 5.14**: su `CuraEngine` y su Python requieren
**glibc ≥ 2.38**. En Ubuntu 22.04 (glibc 2.35) el AppImage no llega a abrir. En
esta investigación se consiguió laminar usando el `CuraEngine` del AppImage con
una glibc 2.44 aparte, pero no es un uso soportado. Si tu Cura 5.14 no arranca,
usa un sistema con glibc nueva (Ubuntu 24.04+) o Cura 4.13/5.8 para generar el
G-code: el resultado es igualmente válido para `xyzsend`.

## 5. Qué hace `xyzsend` por dentro

1. Comprueba que el fichero existe y resuelve su ruta absoluta (funciona desde
   cualquier directorio).
2. Si es `.3w`, lo sube directamente.
3. Si es G-code, añade la cabecera `; filename`, `; print_time`, `; machine =
   daVinciAW10`, `; total_layers`, temperaturas y `; layer_height` deducidas del
   propio G-code (si no la trae ya), normaliza a CRLF y cifra el `.3w`
   (AES-256-ECB, CRC-32).
4. Consulta el estado (solo lectura) y sube por `XYZv3/upload` en bloques de
   8192 B.
5. Al terminar envía `XYZv3/uploadDidFinish`: **la impresión arranca ahí**.

Una subida de 2,8 MB tarda ~4–5 min a 115200 baudios.

## 6. Integración con Caja (botón derecho)

`./tools/install-caja.sh` copia `packaging/caja/xyzprint-caja.py` a
`~/.local/share/caja-python/extensions/` y reinicia Caja. A partir de ahí,
**botón derecho sobre un `.gcode` o `.3w` → "Imprimir en la da Vinci 1.0 Pro"**.

Cómo funciona:

1. Elige el fichero (una sola selección, extensión `.gcode`, `.gco`, `.nc`,
   `.g`, `.3w`) y comprueba que `xyzsend` exista.
2. Pregunta con `zenity` mostrando nombre, tamaño y tiempo estimado.
3. Abre una terminal con `xyzsend` para ver el progreso (mate-terminal,
   gnome-terminal, xfce4-terminal, konsole o xterm).
4. Si no hay terminal, ejecuta `xyzsend --yes` en segundo plano y avisa con
   `notify-send`.

La extensión **no reimplementa** nada del protocolo: solo llama a `xyzsend`, así
que cualquier cambio en el `.3w` o en el USB se hace en un único sitio.

Requisitos: `python3-caja` (paquete de Ubuntu/MATE), `zenity` para el diálogo y
`xyzsend` instalado. Desinstalar:

```sh
./tools/install-caja.sh --uninstall
caja -q      # y volver a abrir
```

Si la entrada no aparece, lanza `caja` desde una terminal: los errores de las
extensiones Python salen por stderr.

## 7. Problemas

| Síntoma | Causa / solución |
|---|---|
| `no existe /dev/ttyACM0` | impresora apagada, cable, o `lsusb` no la ve (`11f1:2520`) |
| `sin permisos` | falta el grupo `dialout` (cerrar sesión tras `usermod`) |
| `cabecera de subida: la impresora abortó (E0)` | la impresora rechaza un trabajo nuevo: está imprimiendo, tiene un trabajo a medias o hay que apagarla y encenderla |
| `aviso: el G-code no contiene M108/M191` | el Start G-code de Cura no es el de §4 |
| `no se encontró "G21 ;"` | falta `G21 ;` en el Start G-code (no vale `G21` a secas) |
| `Trying to retrieve setting with no value given: roofing_layer_count` | perfil desactualizado: usa el que incluye `roofing_layer_count` y `flooring_layer_count` |
| el AppImage de Cura 5.14 no abre | exige glibc ≥ 2.38; usa Ubuntu 24.04+ u otra versión de Cura (ver §4.1) |
| `crc32 inválido` en `inspect` | `.3w` corrupto o clave AES distinta de la de fábrica |

Tras un corte a mitad de subida, la impresora puede quedarse en modo subida:
apágala y enciéndela antes de reintentar.
