# Perfil y máquina — da Vinci 1.0 Pro

## Parámetros de la máquina

| Dato | Valor | Origen |
|---|---|---|
| Cama | 200 × 200 × 200 mm | especificación del modelo `dvF1W0A000` |
| Centro de impresión | 100, 100 mm | perfil de la máquina |
| Boquilla | 0,4 mm | perfiles de material del fabricante |
| Cama caliente | sí | observado |
| Filamento | 1,75 mm | observado |
| Firmware | 1.4.2 / UI 5.1.5 | `taginfo` |

## Perfil incluido

`profiles/davinci-1.0-pro-pla.json` es un perfil completo para CuraEngine 4.13:

| Ajuste | Valor |
|---|---|
| Altura de capa | 0,2 mm (primera capa 0,3) |
| Ancho de línea | 0,4 mm |
| Paredes | 2 líneas, 3 capas arriba/abajo |
| Relleno | 10 %, patrón *grid* |
| Velocidad | 30 mm/s (primera capa 20, desplazamientos 100) |
| Temperatura | 210 °C boquilla, 45 °C cama |
| Retracción | 4 mm a 30 mm/s, mínimo 2 mm de recorrido |
| Adhesión | *skirt* de 2 líneas a 3 mm |
| Ventilador | 100 % (apagado en la primera capa) |

La secuencia de arranque reproduce la de un `.3w` real:

```
G21 ;
M107
M108
M140 S45
M190 S45
M104 S210
M109 S210
M191
G90
G92 E0
M82
```

(`slice` sustituye los placeholders `{material_*}` antes de llamar a CuraEngine.)

## Cambiar el perfil

Copia `profiles/davinci-1.0-pro-pla.json`, edita los valores y pásalo con
`--profile`:

```sh
xyzprint-cli slice modelo.stl modelo.3w \
    --profile mi-perfil.json \
    --set center_object=true
```

También se puede ajustar un valor suelto sin duplicar el fichero:

```sh
xyzprint-cli slice modelo.stl modelo.3w \
    --set material_print_temperature=205 \
    --set infill_sparse_density=20 \
    --set center_object=true
```

Al laminar, `--set center_object=true` centra el modelo en la cama. Sin ese
ajuste, el STL se coloca con la esquina en (100, 100) y se sale del área útil.

## Notas de calibración

- El cubo de prueba (10 mm) se imprimió correctamente con este perfil.
- Si las esquinas se levantan: sube la cama 5 °C o usa `adhesion_type: brim`.
- Si hay hilos entre torres: baja la boquilla 5 °C o sube `retraction_amount`.
- La impresora muestra en el LCD la cama a 40 °C aunque el perfil pida 45;
  parece un redondeo propio del firmware. No afecta a la impresión.
- CuraEngine por CLI no calcula tiempo ni filamento: `slice` los estima del
  G-code (±15–25 % en tiempo, por ignorar aceleraciones).
