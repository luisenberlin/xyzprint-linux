# xyzprint — da Vinci 1.0 Pro desde Linux

Imprime en la **XYZprinting da Vinci 1.0 Pro** desde Linux sin Wine, sin máquina
virtual y sin el software oficial de Windows: lamina con CuraEngine, empaqueta el
`.3w` y lo envía por USB directamente a la impresora.

> **Estado**: probado en una impresora real (modelo `dvF1W0A000`, firmware
> 1.4.2 / UI 5.1.5). La cadena completa —slice → `.3w` → USB → imprimir— está
> validada; ver [Estado](#estado).

## Qué hace

- **`slice`**: lamina un STL/OBJ/3MF con CuraEngine usando el perfil de la
  1.0 Pro (cama 200×200×200, boquilla 0,4 mm, PLA) y genera el `.3w`.
- **`wrap` / `encode` / `decode` / `inspect`**: construyen, extraen y validan
  `.3w` sin laminar (por ejemplo, G-code venido de Cura, PrusaSlicer o Slic3r).
- **`send` / `upload`**: suben el `.3w` por `/dev/ttyACM0` y **arrancan la
  impresión** al terminar (`uploadDidFinish`).
- **`status`**, **`pause`**, **`resume`**, **`cancel`**, **`complete`**: consultan
  y controlan el trabajo.
- **`xyzsend`**: un solo comando, `xyzsend modelo.gcode` (empaqueta si hace
  falta, sube y muestra cómo seguir).
- **Menú contextual de Caja**: clic derecho sobre un `.gcode` o `.3w` →
  *Imprimir en la da Vinci*.

## Estado

| Pieza | Estado |
|---|---|
| Protocolo USB serie | reconstruido y verificado contra la impresora real |
| Formato `.3w` | validado byte a byte contra ficheros reales |
| Laminado (`slice`) | funcional con CuraEngine 4.13 |
| Envío y arranque de impresión | **probado imprimiendo un cubo real** (2026-11-18) |
| Estimación de tiempo/filamento | calculada del G-code (±15–25 % de tiempo) |
| Integración con Caja | funcional |
| Interfaz gráfica | pendiente (CLI + Caja por ahora) |
| Plugin de Cura | pendiente |

## Requisitos

- Linux x86_64 (probado en Debian/Ubuntu con Qt 6 y CuraEngine 4.13).
- Qt 6 (`Core` y `SerialPort`) y OpenSSL.
- `cura-engine` solo si vas a laminar (`slice`).
- Usuario en el grupo `dialout` para acceder a `/dev/ttyACM0`.
- La impresora conectada por USB (VID:PID `11f1:2520`).

Instalación de dependencias en Debian/Ubuntu:

```sh
sudo apt install build-essential cmake ninja-build \
                 qt6-base-dev qt6-serialport-dev libssl-dev \
                 cura-engine zenity
```

`zenity` es opcional: lo usa el menú de Caja para confirmar antes de imprimir.

## Instalación rápida

### Desde el paquete

```sh
tar xf xyzprint-0.1.0-linux-x86_64.tar.gz
cd xyzprint-0.1.0-linux-x86_64
./tools/install-local.sh
```

### Desde el código fuente

```sh
cmake -S src -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./tools/install-local.sh
```

Ambas rutas instalan en `~/.local/opt/xyzprint/{bin,lib,share/xyzprint/profiles}`
y dejan `xyzprint-cli` y `xyzsend` enlazados en `~/.local/bin`. Si ese directorio
está en el `PATH` (lo normal en Debian/Ubuntu), los comandos funcionan desde
cualquier carpeta.

Opción portable: `./tools/install-local.sh --bundle` copia también las
bibliotecas Qt6/OpenSSL necesarias para llevarlo a otro equipo sin instalar Qt.

## Uso

```sh
# Estado de la impresora
xyzprint-cli status --port /dev/ttyACM0

# Laminar y generar .3w (centrado en la cama)
xyzprint-cli slice modelo.stl modelo.3w --set center_object=true

# Imprimir un G-code o un .3w ya hecho (pide confirmación)
xyzsend modelo.gcode

# Sin preguntas (para scripts o Caja)
xyzsend -y modelo.gcode

# Solo empaquetar, sin enviar
xyzsend --dry-run modelo.gcode

# Pausar, reanudar y cancelar
xyzprint-cli pause  --port /dev/ttyACM0
xyzprint-cli resume --port /dev/ttyACM0
xyzprint-cli cancel --port /dev/ttyACM0
```

Notas de uso:

- La impresión **arranca al terminar la subida**; no hay un comando aparte de
  «start».
- Antes de subir, la impresora debe estar en reposo (`estado: 9511`). Al acabar
  un trabajo se queda en `9509` hasta que pulsas **OK** en el LCD.
- `xyzsend` sin terminal interactiva (por ejemplo, desde Caja) asume `-y`.

## Menú de Caja

```sh
./tools/install-caja.sh
```

Instala la extensión en `~/.local/share/caja-python/extensions/` y reinicia Caja.
Requiere `python3-caja` (paquete `python3-caja` en Debian/Ubuntu).

## Usar G-code de Cura

Si prefieres laminar en la interfaz de Cura, guarda el G-code y envíalo con
`xyzsend`. El Start G-code debe incluir `M108`, `M191` y `G21 ;` para que la
impresora arranque bien. La guía completa, con el Start/End G-code listo para
copiar, está en [docs/imprimir-desde-cura.md](docs/imprimir-desde-cura.md).

## Estructura del repositorio

```
src/                  CLI en C++/Qt6 (protocolo serie, códec .3w, laminado)
src/tests/            tests del códec .3w y del perfil
tools/                xyzsend, instaladores, empaquetado, referencia .3w en Node
profiles/             perfil de CuraEngine para la 1.0 Pro (PLA, 0,4 mm)
packaging/caja/       extensión Python para el menú contextual de Caja
packaging/udev/       regla udev opcional
docs/                 documentación técnica y guías
how-to.md             guía de instalación, uso y distribución
```

## Documentación

- [how-to.md](how-to.md) — instalación, uso, Caja, Cura, distribución y problemas.
- [docs/linux-build.md](docs/linux-build.md) — compilar y usar la CLI.
- [docs/imprimir-desde-cura.md](docs/imprimir-desde-cura.md) — Cura + `xyzsend`.
- [docs/formato-3w.md](docs/formato-3w.md) — formato `.3w` (cabecera y cifrado).
- [docs/protocolo-serie.md](docs/protocolo-serie.md) — protocolo USB serie y estados.
- [docs/perfiles-y-maquina.md](docs/perfiles-y-maquina.md) — perfil y parámetros.
- [docs/plan.md](docs/plan.md) — hoja de ruta (UI, AppImage, plugin de Cura).

## Compilar y probar

```sh
cmake -S src -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Los tests no necesitan impresora. Para comprobar el perfil sin Qt:

```sh
node tools/check-profile.mjs     # requiere Node 14+
```

## Crear un paquete para otro equipo

```sh
./tools/make-dist.sh                 # binario + perfiles + scripts + docs
./tools/make-dist.sh --with-libs     # además las bibliotecas Qt6/OpenSSL
./tools/make-dist.sh --with-source   # además el código fuente
```

Genera `dist/xyzprint-<versión>-linux-x86_64.tar.gz` y su `.sha256`.

## Aviso legal

Este es un proyecto **independiente**, sin relación con XYZprinting ni con sus
fabricantes. No contiene ni redistribuye binarios, DLL, instaladores ni código
fuente de XYZprinting: el protocolo y el formato `.3w` se han reconstruido por
interoperabilidad a partir del comportamiento observable del software y de la
propia impresora, y se documentan aquí para que otros puedan imprimir con
hardware que ya poseen.

El software se ofrece **tal cual**, sin garantía. Imprimir implica riesgos
físicos (calor, partes móviles, filamento): úsalo bajo tu responsabilidad.

## Licencia

MIT — ver [LICENSE](LICENSE).
