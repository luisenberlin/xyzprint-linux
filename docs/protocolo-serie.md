# Protocolo serie/USB — da Vinci 1.0 Pro

> Reconstruido y verificado contra la impresora real, e impreso con éxito.
> Implementación de referencia: `src/xyzserial.cpp` (C++/Qt6). Codificador `.3w`:
> `tools/threew.mjs` y `src/threew.cpp`.

## 1. Capa física

| Parámetro | Valor | Origen |
|---|---|---|
| VID/PID | `11F1:2520` | descriptor USB observado |
| Driver Windows | `XYZ_Devices_CDC.inf` → `usbser.sys` | registro del sistema |
| Puerto | CDC-ACM (`/dev/ttyACM*` en Linux) | observado |
| Baudrate | **115200** | observado y confirmado en la configuración del driver |
| Datos/paridad/stop | **8N1** | observado |
| Flow control | **ninguno** | observado |
| DTR/RTS | afirmados por el host | observado (sin DTR el CDC no entrega datos) |

En Linux el dispositivo aparece como `/dev/ttyACM*`; no hace falta driver
propietario, solo estar en el grupo `dialout` (o una regla udev; ver
`packaging/udev/99-xyzprinting.rules`).

## 2. Tramas de comando

Los comandos son **ASCII plano, sin terminador**. El firmware delimita por
formato, no por salto de línea (probado: `XYZv3/query=j` funciona con y sin
`\r\n`).

| Comando | Respuesta observada | Función |
|---|---|---|
| `XYZv3/query=j` | `j:9505\n$\n` | estado / ocupado |
| `XYZv3/query=w` | `w:1,--------------\n$\n` | cartucho 1 (nombre) |
| `XYZv3/query=O` | `O:{"nozzle":"210","bed":"40"}\n$\n` | temperaturas |
| `XYZv3/action=<x>` | — | acciones de UI |
| `XYZv3/config=<x>` | — | configuración |
| `XYZv3/config=print:[cancel]` | — | cancela la impresión |
| `XYZv3/config=print:[pause]` | — | pausa |
| `XYZv3/config=print:[resume]` | — | reanuda |
| `XYZv3/config=print:[complete]` | — | marca el trabajo como completado |
| `XYZv3/upload=<nombre>,<bytes>` | `ok` | inicia subida |
| `<paquete binario>` | `ok` / `no` | un paquete de datos |
| `XYZv3/uploadDidFinish` | — (no se espera ACK) | cierra la subida y **arranca la impresión** |

Las respuestas de consulta llevan el prefijo `<clave>:` y terminan en `\n$\n`.
El `$` actúa como marca de fin de respuesta.

### 2.1 `taginfo` — estado completo (observado en vivo)

`XYZv3/query=taginfo` devuelve un bloque multilínea `clave:valor` terminado en `$`:

```text
b:63
d:31,57,123
e:0
f:1,0
i:3F1AWPEU4TH63AT0XX
j:9505
L:1,447840,30826
m:0,0,0
o:p8,t1,c1,a-
O:{"nozzle":"210","bed":"40"}
p:dvF1W0A000
s:{"fm":1,"fd":1,"dr":{"top":"off","front":"off"},"sd":"yes","eh":"0","of":"1"}
t:1,193
v:1.4.2
w:1,--------------
n:daVinci Pro. 1w
X:2,GB-0004-0000-TH-638-0098-26039
l:es
V:5.1.5
4:{"wlan":{"ip":"0.0.0.0","ssid":"","channel":"0","MAC":"20:f8:5e:xx:xx:xx"}}
$
```

| Clave | Valor observado | Interpretación |
|---|---|---|
| `i` | `3F1AWPEU4TH63AT0XX` | número de serie (coincide con el descriptor USB) |
| `n` | `daVinci Pro. 1w` | nombre comercial |
| `p` | `dvF1W0A000` | código de modelo; se mapea a `daVinciAW10` |
| `v` | `1.4.2` | firmware de la placa |
| `V` | `5.1.5` | firmware de la interfaz |
| `j` | `9505` | estado (ver §2.2) |
| `O` | `{"nozzle":"210","bed":"40"}` | temperaturas objetivo |
| `w` | `1,--------------` | cartucho 1 (nombre vacío) |
| `s` | `{"fm":1,"fd":1,"dr":{...},"sd":"yes","eh":"0","of":"1"}` | flags de sensores/puertas |
| `4` | `{"wlan":{...}}` | interfaz de red |
| `l` | `es` | idioma |
| `b`,`d`,`e`,`f`,`L`,`m`,`o`,`t`,`X` | varios | **desconocido**; `b`, `d` y `t` cambian entre lecturas (valores en vivo) |

El parser oficial reconoce además los marcadores de error `$E0` y `E2$` y líneas
con prefijos `4:`, `N:`, `sd:`, `eh:`, `of:`, `buzzer:`, `dr:`, `front:`, `top:`.

### 2.2 Códigos de `j` observados en vivo

Secuencia real durante una impresión de prueba (cubo de 10 mm, 2026-11-18):

| `j` | Momento | Evidencia |
|---|---|---|
| `9511` | reposo | `d:0,0,0`, cama a la temperatura objetivo, sin trabajo activo |
| `9500` | justo tras `uploadDidFinish` | aún calentando/preparando |
| `9501` | calentando | `d:0,0,0`; `b` y `t` subiendo |
| `9505` | imprimiendo | `d` cambia entre lecturas (p. ej. `9,5,33874`) |
| `9509` | trabajo terminado | `d:0,0,0` (cabeza aparcada en `100,10,0`), cama enfriando |

`9511` es el único estado en el que se ha conseguido subir un `.3w`. Con `9505`
y `9509` la subida se rechaza con `E0`. `print:[complete]` por USB no devuelve la
impresora a reposo; lo que sí funciona (observado) es **pulsar OK en el LCD**,
que la devuelve a `9511`.

## 3. Paquetes de datos

Cada paquete tiene **12 bytes de cabecera + payload**:

```
offset  tamaño  contenido
0       4       índice de paquete, uint32 big-endian (empieza en 0)
4       4       longitud del payload, uint32 big-endian
8       N       payload
8+N     4       CRC-32 del payload, uint32 big-endian
```

- Tamaño de chunk: **8192 bytes** (`0x2000`) para `.3w`.
- CRC-32: polinomio `0xEDB88320`, init `0xFFFFFFFF`, XOR final.
- El driver oficial de Windows envía el campo CRC a **cero** y el firmware lo
  acepta; `xyzprint` envía el CRC real.

## 4. Secuencia de impresión

1. Abrir el puerto (115200 8N1).
2. Enviar `XYZv3/upload=<nombre fichero>,<tamaño total>`.
3. Esperar ACK que contenga `ok` (hasta 10 intentos).
4. Para cada chunk de 8192 B: enviar paquete y esperar `ok`.
   - `no` → reenviar; `E0` → abortar; `E35` → continuar.
5. Enviar `XYZv3/uploadDidFinish`.
6. Esperar 500 ms y cerrar el puerto.

**La impresora arranca la impresión al recibir `uploadDidFinish`**; no hay un
comando separado de «start» en la ruta USB. Cancelar = `XYZv3/config=print:[cancel]`.

## 5. Observado / inferido / desconocido

- **Observado**: baudrate, 8N1, comandos `j`/`w`/`O`, formato de respuesta,
  framing de 12 bytes, la secuencia completa de subida, los estados de §2.2 y
  que la impresora imprime un `.3w` generado por este proyecto.
- **Inferido (alta confianza)**: la impresión arranca con `uploadDidFinish`
  (no aparece ningún comando de start en la ruta USB).
- **Desconocido**: significado exacto de los bits de `j`; tolerancia del firmware
  a CRC=0 frente a CRC real; comportamiento con `.3w` de otras claves.
