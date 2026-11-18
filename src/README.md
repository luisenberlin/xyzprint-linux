# src/ — CLI de referencia (Qt6)

Implementación **reconstruida** del transporte USB serie y del formato `.3w` de
la XYZprinting da Vinci 1.0 Pro. No es código fuente de XYZprinting; ver
`docs/protocolo-serie.md` y `docs/formato-3w.md`.

## Dependencias

Debian/Ubuntu:

```sh
sudo apt install build-essential cmake ninja-build \
    qt6-base-dev qt6-serialport-dev libssl-dev cura-engine
```

`cura-engine` solo es necesario para `slice`; `ports`, `status`, `upload`,
`wrap`, `encode`, `decode` e `inspect` funcionan sin él. `slice` necesita además
`fdmprinter.def.json` y `fdmextruder.def.json`: suelen estar en
`/usr/share/cura/resources/definitions` (paquete `cura`) o se indican con
`--definitions`.

## Compilar

```sh
cmake -S src -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

`ctest` valida el códec `.3w` contra dos vectores generados por el DLL original
(SHA-256 byte a byte) y comprueba el perfil de CuraEngine de la 1.0 Pro.

Sin Qt ni CuraEngine, el perfil se puede validar con Node:

```sh
node tools/check-profile.mjs
```

## Uso

```sh
# Puertos serie detectados (busca /dev/ttyACM0)
./build/xyzprint-cli ports

# Estado completo: serie, modelo, firmware, red, sensores
./build/xyzprint-cli status --port /dev/ttyACM0

# Consulta cruda
./build/xyzprint-cli query j --port /dev/ttyACM0

# .3w: laminar con CuraEngine y empaquetar
./build/xyzprint-cli slice modelo.stl modelo.3w \
    --cura /usr/bin/CuraEngine \
    --definitions /usr/share/cura/resources/definitions \
    --keep-gcode modelo.gcode

# .3w: empaquetar G-code de Cura/PrusaSlicer, inspeccionar, decodificar, codificar
./build/xyzprint-cli wrap modelo.gcode modelo.3w
./build/xyzprint-cli inspect modelo.3w
./build/xyzprint-cli decode modelo.3w modelo.gcode
./build/xyzprint-cli encode modelo.gcode modelo.3w

# Cancelar / pausar / reanudar / completar
./build/xyzprint-cli cancel   --port /dev/ttyACM0
./build/xyzprint-cli pause    --port /dev/ttyACM0
./build/xyzprint-cli resume   --port /dev/ttyACM0
./build/xyzprint-cli complete --port /dev/ttyACM0

# Subir e IMPRIMIR (requiere --yes de forma deliberada)
./build/xyzprint-cli upload modelo.3w --port /dev/ttyACM0 --yes
```

`upload` envía `XYZv3/upload=...`, los paquetes de 8192 B con CRC-32 y, al
terminar, `XYZv3/uploadDidFinish`; la impresora arranca la impresión en ese
momento. Sin `--yes` la CLI se niega a subir.

`slice` y `wrap` normalizan el G-code a CRLF, como el `.3w` real de XYZprint;
`encode`/`decode` conservan los bytes para reproducir los vectores del DLL.

## Permisos en Linux

```sh
sudo install -m 0644 packaging/udev/99-xyzprinting.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

La regla usa `MODE="0666"` y `uaccess`, así que no hace falta grupo. Si prefieres
`0660`, añade el grupo de tu distro (`dialout` en Debian/Ubuntu, `uucp` en Arch)
y mete tu usuario en él.

## Estado

- `.3w`: validado byte a byte contra el DLL original.
- Protocolo serie: verificado contra la impresora real (`j`, `w`, `O`, `taginfo`).
- Laminado: `slice` invoca CuraEngine con `profiles/davinci-1.0-pro-pla.json`
  (200×200×200, PLA 210/45, retracción 4 mm, arranque `M108`/`M191`).
- Subida real: pendiente de prueba con un modelo laminado.
