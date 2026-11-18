# Formato `.3w` — especificación (da Vinci 1.0 Pro)

> Validado byte a byte contra ficheros `.3w` reales y contra el comportamiento
> del software oficial, e impreso con éxito en una da Vinci 1.0 Pro.
> Implementación ejecutable de referencia: `tools/threew.mjs` (Node, sin
> dependencias externas) y `src/threew.cpp` (C++/Qt6).
>
> Este documento describe la ruta que usa la 1.0 Pro (`; machine = daVinciAW10`
> → código interno `FPro` = `0x4650726F`). Otros modelos usan la ruta comprimida
> `a128` (ver más abajo).

## Firma

```
3DPFNKG13WTW
```

## Estructura del fichero

Cabecera fija de **8192 bytes** (`0x2000`) seguida del payload cifrado.
Todos los enteros multibyte están en **big-endian**.

| Offset | Tamaño | Valor observado | Significado |
|---:|---:|---|---|
| `0x0000` | 12 | `3DPFNKG13WTW` | magic |
| `0x000C` | 2 | `01 05` | versión 1.5 |
| `0x000E` | 2 | `00 00` | reservado |
| `0x0010` | 4 | `00 00 00 08` | constante 8 |
| `0x0014` | 8 | ceros | reservado |
| `0x001C` | 4 | `TagE` | marca de cabecera |
| `0x0020` | 4 | `J256` / `a128` | tamaño de clave: AES-256 / AES-128 |
| `0x0024` | 4 | p. ej. `00 00 00 2D` | longitud del texto de cabecera |
| `0x0028` | 4 | `00 00 00 44` | constante 0x44 |
| `0x002C` | 4 | `00 00 00 01` | constante 1 |
| `0x0030` | 4 | CRC-32 | checksum del payload **cifrado** |
| `0x0034` | `0x3C` | ceros | reservado |
| `0x0070` | N | texto ASCII | prefijo del G-code hasta `G21 ;` (exclusive) + NUL |
| `0x2000` | resto | ciphertext | payload AES |

El campo `0x24` es el offset de `"G21 ;"` dentro del G-code, es decir, la longitud
del prefijo de comentarios que se copia en la cabecera. El NUL se escribe en
`0x70 + longitud`.

## Payload

- **AES-256-ECB**, sin IV y sin encadenamiento entre bloques.
- Clave por defecto: `@xyzprinting.com@xyzprinting.com` (32 bytes ASCII), la
  misma que trae embebida la herramienta oficial para la 1.0 Pro. `xyzprint`
  la usa porque la impresora no acepta `.3w` cifrados con otra clave.
- **Relleno PKCS#7** con una particularidad:
  - `encLen = ceil(n / 16) * 16`.
  - Si `n % 16 != 0`, los bytes `[n, encLen)` se rellenan con el valor `encLen - n`.
  - Si `n % 16 == 0`, **no se añade bloque de relleno**.
- Al descifrar, el software oficial recorta el relleno solo si el último byte está
  en `2..16`; un relleno de 1 byte se deja tal cual (comportamiento observado).

## Checksum

**CRC-32 estándar** sobre el ciphertext completo (`encLen` bytes), no sobre el
G-code:

- polinomio `0xEDB88320` (reflejado),
- valor inicial `0`,
- XOR final.

## Ruta de otros modelos (`a128`)

Los modelos distintos de la 1.0 Pro empaquetan así:

```
gcode → deflate (miniz) → AES-128 → base64 → cabecera con "a128"
```

La 1.0 Pro **no** usa esta ruta: detecta `; machine = daVinciAW10`, resuelve
`FPro` y cifra el G-code tal cual con AES-256-ECB. Por eso el prefijo de
comentarios (`; filename`, `; print_time`, `; machine = daVinciAW10`, …) viaja
en claro en la cabecera.

## Cabecera de texto

`slice`, `wrap` y `send` generan esta cabecera, que es la que espera la
impresora para mostrar nombre, tiempo y temperaturas en el LCD:

```
; filename = modelo.gcode
; print_time = 423
; machine = daVinciAW10
; total_layers = 50
; nozzle_diameter = 0.4
; temperature = 210
; bed_temperature = 40
; layer_height = 0.2
; total_filament = 292.29
; generated_by = xyzprint
G21 ;
...
```

`print_time` va en segundos y `total_filament` en milímetros.

## Validación

Vectores generados con la clave por defecto:

| G-code | Bytes | `.3w` | SHA-256 del `.3w` |
|---|---:|---:|---|
| `flat.gcode` (sin saltos de línea) | 240 | 8432 | `B45D6FB477097B309F3D9730E0AD289D03C66E48763812A970E5100F7FB5BFE5` |
| `test.gcode` (LF, 9 bytes de relleno) | 231 | 8432 | `9FB6CC4102858DFDE35A92C4C1F8C005ABECDFD9297751E2FC28AAB46CA3D930` |

`node tools/threew.mjs encode` reproduce ambos ficheros **idénticos byte a byte**;
`decode` recupera el G-code original exacto. Los tests de `src/tests/` cubren el
mismo caso desde C++ (`ctest --test-dir build`).

## Pendiente / desconocido

1. Si el software oficial permite pasar a la 1.0 Pro una clave distinta de la de
   fábrica. El parámetro existe, pero la clave embebida es el *fallback*.
2. DRM: la ruta `FPro` no usa la capa `NKGDRM*`, y en la práctica la impresora
   acepta `.3w` generados por `xyzprint` sin ella (probado imprimiendo).
