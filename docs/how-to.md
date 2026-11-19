# how-to — instalar, usar y distribuir xyzprint

`xyzprint-cli` es una CLI libre (MIT) para imprimir en la **XYZprinting da Vinci
1.0 Pro** desde Linux, sin Wine ni máquinas virtuales. Lamina con CuraEngine,
empaqueta el `.3w` cifrado y lo sube por USB con el protocolo `XYZv3`.

Flujo completo:

```
modelo.stl ──Cura──► pieza.gcode ──xyzsend──► pieza.3w ──USB──► da Vinci 1.0 Pro
```

Para el detalle de Cura y del protocolo, ver
[`imprimir-desde-cura.md`](imprimir-desde-cura.md),
[`linux-build.md`](linux-build.md) y
[`protocolo-serie.md`](protocolo-serie.md).

---

## 1. Requisitos

| | Enviar e imprimir | Laminar (`slice`) |
|---|---|---|
| Librerías | Qt6 Core + SerialPort, OpenSSL | igual |
| Programas | — | `CuraEngine` + definiciones de Cura |
| Permisos | usuario en el grupo `dialout` | — |
| Hardware | da Vinci 1.0 Pro (`11f1:2520`) por USB | — |

Debian / Ubuntu 22.04:

```sh
sudo apt install libqt6core6 libqt6serialport6 libssl3   # enviar
sudo apt install cura-engine cura                        # laminar
sudo usermod -aG dialout "$USER"                         # cerrar sesión después
```

Fedora / Arch: equivalentes (`qt6-qtbase`, `qt6-qtserialport`, `openssl`,
`cura-engine`). Las definiciones de Cura suelen venir con la GUI de Cura.

---

## 2. Instalación desde un paquete

```sh
tar xf xyzprint-0.1.0-linux-x86_64.tar.gz
cd xyzprint-0.1.0-linux-x86_64
./tools/install-local.sh
```

Deja todo en `~/.local/opt/xyzprint/` y enlaces en `~/.local/bin/`:

```
~/.local/opt/xyzprint/
├── bin/xyzprint-cli        CLI (ports, status, slice, wrap, send, inspect…)
├── bin/xyzsend             atajo: empaquetar + imprimir
├── share/xyzprint/profiles/davinci-1.0-pro-pla.json
└── lib/                    (solo si el paquete se hizo con --with-libs)
```

Opciones del instalador: `--prefix RUTA`, `--bindir RUTA`, `--bundle`,
`--uninstall`.

Si `~/.local/bin` no está en el `PATH`:

```sh
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.profile
```

---

## 3. Instalación desde el código fuente

```sh
sudo apt install build-essential cmake ninja-build \
    qt6-base-dev qt6-serialport-dev libssl-dev

cmake -S src -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./tools/install-local.sh
```

Los tests deben dar **2/2** (`threew`, `profile`).

---

## 4. Uso

```sh
xyzsend pieza.gcode             # pregunta y envía
xyzsend -y pieza.gcode          # sin preguntar
xyzsend -y pieza.3w             # reimprime un .3w ya empaquetado
xyzsend --dry-run pieza.gcode   # crea y valida el .3w, NO imprime
```

| Comando | Para qué |
|---|---|
| `xyzprint-cli status` | estado, temperaturas, firmware, trabajo |
| `xyzprint-cli ports` | puertos serie detectados |
| `xyzprint-cli slice m.stl m.3w` | laminar sin Cura (perfil propio) |
| `xyzprint-cli send m.gcode --yes` | empaquetar + subir |
| `xyzprint-cli wrap in.gcode out.3w` | solo empaquetar |
| `xyzprint-cli inspect m.3w` | cabecera y CRC |
| `xyzprint-cli pause` / `resume` / `cancel` | control de la impresión |

**La impresión arranca al terminar la subida** (`XYZv3/uploadDidFinish`); no hay
un comando de *start* aparte. Una subida de 3 MB tarda ~5 min a 115200 baudios.

---

## 5. Integración con Caja (botón derecho)

```sh
./tools/install-caja.sh
```

Copia la extensión a `~/.local/share/caja-python/extensions/` y reinicia Caja.
Después, **botón derecho sobre un `.gcode` o `.3w` → "Imprimir en la da Vinci
1.0 Pro"**:

1. Muestra un diálogo con el nombre, el tamaño y el tiempo estimado (zenity).
2. Si aceptas, abre una terminal con `xyzsend` para ver el progreso.
3. Sin terminal disponible, imprime en segundo plano y avisa con `notify-send`.

Requisitos: `python3-caja` (ya viene en Ubuntu/MATE), `zenity` (opcional) y
`xyzsend` instalado. Desinstalar: `./tools/install-caja.sh --uninstall`.

Si la entrada no aparece: ejecuta `caja -q` y vuelve a abrir una carpeta; para
ver errores de la extensión, lanza `caja` desde una terminal y mira la salida.

---

## 6. Configurar Cura 4.13

Impresora **Custom FFF** (no la definición "XYZprinting" que trae Cura):

| Ajuste | Valor |
|---|---|
| X / Y / Z | 200 / 200 / 200 mm |
| Origin at center | desmarcado |
| G-code flavor | RepRap (Marlin/Sprinter) |
| Cama caliente | sí · boquilla 0,4 mm · filamento 1,75 mm |

Start G-code (el `G21 ;` es obligatorio):

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

End G-code:

```gcode
M104 S0
M140 S0
G92 E0
G1 F1800.000 E-4.00000
G92 E0
M84
```

Luego *File → Save to Disk* y `xyzsend` sobre el `.gcode`.

---

## 7. Crear el paquete para distribuir

```sh
./tools/make-dist.sh                 # compila, testea y empaqueta
./tools/make-dist.sh --with-libs     # + librerías Qt6/OpenSSL (portable)
./tools/make-dist.sh --with-source   # + src/ y docs/ (para que otros compilen)
```

Salida en `dist/`:

```
xyzprint-0.1.0-linux-x86_64/
xyzprint-0.1.0-linux-x86_64.tar.gz
xyzprint-0.1.0-linux-x86_64.tar.gz.sha256
```

El script compila en Release, **ejecuta los tests** y solo empaqueta si pasan.

### Qué incluye

| Ruta | Contenido |
|---|---|
| `bin/` | `xyzprint-cli`, `xyzsend` |
| `share/xyzprint/profiles/` | perfil de CuraEngine |
| `tools/` | `install-local.sh`, `install-caja.sh` |
| `packaging/caja/` | extensión de Caja |
| `docs/`, `README.md`, `LICENSE` | documentación y licencia |
| `lib/` | solo con `--with-libs` |
| `src/`, `docs/` | solo con `--with-source` |

### Sin `--with-libs` o con él

- **Sin `--with-libs`** (~200 KB): el equipo destino necesita Qt6 Core/SerialPort
  y OpenSSL instalados. Es lo recomendable si controlas el destino.
- **Con `--with-libs`** (~20 MB comprimido): no necesita Qt6 instalado; solo
  glibc igual o más nueva y la misma arquitectura (x86_64). Ideal para un equipo
  desconocido o una versión antigua de Ubuntu.

En ambos casos, para **laminar** hay que instalar `cura-engine` y `cura` en el
destino (no se empaquetan: son ~100 MB y sus definiciones cambian con la
versión de Cura).

### Publicar

```sh
sha256sum -c xyzprint-0.1.0-linux-x86_64.tar.gz.sha256
```

Sube el `.tar.gz` y el `.sha256` a donde vayas a distribuirlo (GitHub Releases,
una carpeta compartida…). Antes de hacerlo público:

- [x] `LICENSE` MIT en la raíz.
- [x] Código propio, sin binarios, DLLs ni recursos del fabricante: el paquete
      público solo lleva `src/`, `docs/` y herramientas propias.
- [ ] Probar en un equipo limpio (ver §8).
- [ ] Etiquetar una versión (`git tag v0.1.0`) y adjuntar el `.tar.gz` y su
      `.sha256` a la release de GitHub.
- [ ] No incluir `.3w` de ejemplo ni perfiles descifrados del fabricante.

### Comprobación en un equipo limpio

```sh
# 1. dependencias (§1)
# 2. descomprimir e instalar
tar xf xyzprint-0.1.0-linux-x86_64.tar.gz
cd xyzprint-0.1.0-linux-x86_64 && ./tools/install-local.sh

# 3. sin impresora: valida el empaquetado y el CRC
xyzsend --dry-run /ruta/pieza.gcode

# 4. con impresora: solo lectura
xyzprint-cli ports
xyzprint-cli status --port /dev/ttyACM0

# 5. impresión de prueba (arranca de verdad)
xyzsend -y /ruta/pieza.gcode
```

---

## 8. Problemas típicos

| Síntoma | Solución |
|---|---|
| `no existe /dev/ttyACM0` | impresora apagada, cable USB, `lsusb \| grep 11f1` |
| `sin permisos` en el puerto | `sudo usermod -aG dialout "$USER"` y reiniciar sesión |
| `cabecera de subida: … abortó (E0)` | hay un trabajo en curso (`9505`) o terminado sin confirmar (`9509`): pulsa **OK** en el LCD hasta que `status` dé `9511`; si no, apaga y enciende |
| `estado: 9509 (trabajo terminado…)` | normal al acabar; pulsa OK en el LCD para volver a `9511` y poder subir otro fichero |
| `no se encontró "G21 ;"` | el Start G-code de Cura no lleva `G21 ;` |
| `aviso: … no contiene M108/M191` | Start G-code genérico; copia el de §6 |
| `crc32 inválido` | `.3w` corrupto o clave AES distinta de la de fábrica |
| La entrada de Caja no aparece | `caja -q`, reabrir; comprobar `python3-caja` |
| `xyzsend: command not found` | falta `~/.local/bin` en el `PATH` (§2) |

---

## 9. Limitaciones conocidas

- El perfil `slice` no aplica el `--rotate 180` de `daVinciAW10.dat`; rota el
  modelo en Cura si es asimétrico.
- CuraEngine invocado por CLI no calcula tiempo ni filamento (`;TIME:6666`,
  `;Filament used: 0m`): `slice` los estima a partir de los movimientos del
  G-code. Al ignorar aceleraciones, el tiempo puede desviarse un 15–25 %.
- No hay UI gráfica todavía: la CLI y Caja son la interfaz.
- La impresión se lanza al terminar la subida; no hay cola de trabajos.
- El plugin de Cura (botón *Print* dentro de Cura) está pendiente; hoy se usa
  *Save to Disk* + `xyzsend`.
