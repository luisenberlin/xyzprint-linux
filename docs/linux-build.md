# Compilar y probar en Linux

Guía para llevar `src/` a una máquina Linux con la da Vinci 1.0 Pro conectada
por USB. El objetivo de esta fase es validar la CLI antes de añadir la UI.

## 1. Dependencias

### Debian / Ubuntu

```sh
sudo apt update
sudo apt install build-essential cmake ninja-build \
    qt6-base-dev qt6-serialport-dev libssl-dev cura-engine
```

`cura-engine` aporta el binario `CuraEngine`. Las definiciones
`fdmprinter.def.json` / `fdmextruder.def.json` suelen venir con el paquete
`cura` (GUI) o dentro del AppImage de Cura; si no aparecen, cópialas del
repositorio de Cura (`resources/definitions`) y pasa `--definitions` (ver §4.1).

### Fedora

```sh
sudo dnf install gcc-c++ cmake ninja-build \
    qt6-qtbase-devel qt6-qtserialport-devel openssl-devel
```

### Arch

```sh
sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-serialport openssl
```

`CuraEngine` solo hace falta para `slice`. En Debian/Ubuntu viene en
`cura-engine`; en Arch está en AUR (`cura-engine`) y en Fedora puede requerir
Copr o compilarlo. Las definiciones (`fdmprinter.def.json`) se instalan con la
GUI de Cura o se copian del repositorio de Cura.

## 2. Compilar

```sh
cmake -S src -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

El binario queda en `build/xyzprint-cli`. Los tests validan el `.3w` contra
vectores generados por el DLL original y comprueban que el perfil de CuraEngine
tiene cama 200×200×200 y la secuencia de arranque correcta.

El perfil también se puede validar sin Qt ni CuraEngine (útil antes de compilar):

```sh
node tools/check-profile.mjs
```

## 3. Permisos del dispositivo

La impresora es un CDC-ACM estándar (`11f1:2520`); el kernel la expone como
`/dev/ttyACM*`.

```sh
sudo install -m 0644 packaging/udev/99-xyzprinting.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

La regla usa `MODE="0666"` y `uaccess`; si prefieres un grupo, cambia el modo a
`0660` y añade el grupo de tu distro (`dialout` en Debian/Ubuntu, `uucp` en
Arch) y a tu usuario a él.

Comprobar:

```sh
ls -l /dev/ttyACM*
./build/xyzprint-cli ports
```

## 4. Prueba sin imprimir

```sh
bash tools/smoke-linux.sh /dev/ttyACM0
```

o manualmente:

```sh
./build/xyzprint-cli status --port /dev/ttyACM0
./build/xyzprint-cli query j --port /dev/ttyACM0
./build/xyzprint-cli inspect modelo.3w
```

`status` usa `XYZv3/query=taginfo` y muestra serie, modelo, firmware, red y
sensores. No calienta ni mueve la impresora.

## 4.1 Laminar con CuraEngine

El perfil `profiles/davinci-1.0-pro-pla.json` describe la 1.0 Pro para
CuraEngine: cama 200×200×200, boquilla 0,4 mm, PLA 210/45, retracción 4 mm y la
secuencia de arranque observada en un `.3w` real (`M108`, `M191`, `M140/M190`,
`M104/M109`).

```sh
./build/xyzprint-cli slice modelo.stl modelo.3w \
    --cura /usr/bin/CuraEngine \
    --definitions /usr/share/cura/resources/definitions \
    --keep-gcode modelo.gcode
```

Si `CuraEngine` está en `PATH` y las definiciones en una ruta habitual, bastan
`--profile` (opcional) y los dos primeros argumentos:

```sh
./build/xyzprint-cli slice modelo.stl modelo.3w
```

Ajustes puntuales sin editar el JSON:

```sh
./build/xyzprint-cli slice modelo.stl modelo.3w \
    --set material_print_temperature_layer_0=220 \
    --set material_bed_temperature_layer_0=50 \
    --set speed_print=40
```

`--set e:clave=valor` fuerza el extrusor; `--set g:clave=valor`, lo global. Sin
prefijo, se actualiza el sitio donde ya exista la clave.

`slice` escribe el `.3w` y, si se pide, el G-code intermedio. El G-code lleva la
cabecera `; filename`, `; machine = daVinciAW10`, `; print_time`, temperaturas y
`G21 ;`, usa CRLF como el `.3w` real y convierte `G0` en `G1` para parecerse al
`.3w` de referencia.

El perfil **no** aplica el `--rotate 180` de `daVinciAW10.dat`: CuraEngine no
tiene una opción global equivalente y `mesh_rotation_matrix` rota alrededor del
origen. Para modelos asimétricos, rota el STL antes de laminar o añade la
rotación en `slicer.cpp`.

### Alternativa: empaquetar G-code ya laminado

Si ya tienes un G-code preparado para la 1.0 Pro (por ejemplo, de una sesión
anterior), se puede empaquetar sin volver a laminar:

```sh
./build/xyzprint-cli wrap modelo.gcode modelo.3w
```

`wrap` antepone `; filename = <nombre>`, `; print_time`, `; machine = daVinciAW10`,
`; total_layers`, temperaturas y `; layer_height` (deducidas del propio G-code)
si el G-code no la trae ya, normaliza los finales de línea a CRLF (como el `.3w`
real) y luego aplica el cifrado `.3w`. No añade la secuencia
`M108`/`M191`: para eso usa `slice` o prepara el G-code con el arranque de
[`perfiles-y-maquina.md`](perfiles-y-maquina.md). El perfil de Cura debe usar
cama de 200×200 mm y boquilla de 0,4 mm.

### `send`: empaquetar y subir en un paso

`send` hace lo mismo que `wrap` y a continuación sube el `.3w`, lo que arranca
la impresión. El `.3w` se deja junto al G-code salvo que se indique `--output`.

```sh
./build/xyzprint-cli send modelo.gcode --yes
./build/xyzprint-cli send modelo.gcode --yes --port /dev/ttyACM0 --name pieza.gcode
```

Si el argumento ya es un `.3w`, se sube directamente. El atajo `tools/xyzsend`
añade la confirmación, el resumen (tamaño, tiempo estimado) y el estado previo;
se instala en `~/.local/bin` con `tools/install-local.sh`. Véase
`docs/imprimir-desde-cura.md`.

## 5. Prueba de impresión (opcional)

Solo cuando estés junto a la impresora y con la cama preparada:

```sh
./build/xyzprint-cli upload modelo.3w --port /dev/ttyACM0 --yes
```

Al terminar la subida se envía `XYZv3/uploadDidFinish` y **la impresora arranca
la impresión**. Para cancelar:

```sh
./build/xyzprint-cli cancel --port /dev/ttyACM0
```

## 6. AppImage (fase posterior)

Cuando exista la UI, el empaquetado previsto es `linuxdeploy` + plugin Qt6,
incluyendo `packaging/udev/99-xyzprinting.rules` y un `.desktop`. La CLI actual
sirve como base para validar el transporte y el `.3w` antes de empaquetar.
