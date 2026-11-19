# Cómo contribuir

¡Gracias por el interés! Este proyecto es pequeño y toda ayuda cuenta, sobre todo las pruebas con otros equipos.

## Pruebas con otras impresoras o sistemas

Hoy solo está validada la da Vinci 1.0 Pro (`dvF1W0A000`, firmware 1.4.2). Si la pruebas con otro modelo, firmware o distribución, abre un issue con:

- la salida de `xyzprint-cli status --port /dev/ttyACM0`,
- tu distribución y versión de Qt y CuraEngine,
- qué funcionó y qué no.

## Informar de un error

Incluye la orden que ejecutaste, el mensaje completo y, si es posible, el estado de la impresora (`status`). Si el problema es al imprimir, indica el estado que muestra el LCD.

## Enviar cambios

1. Haz un fork y crea una rama para tu cambio.
2. Compila y ejecuta los tests (no necesitan impresora):

   ```sh
   node tools/check-profile.mjs
   cmake -S src -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ctest --test-dir build --output-on-failure
   ```

3. Si cambias el protocolo o el formato `.3w`, actualiza `docs/protocolo-serie.md` o `docs/formato-3w.md`.
4. Anota el cambio en `CHANGELOG.md` y abre la pull request.

## Aviso importante

No subas binarios, DLL, instaladores ni código fuente de XYZprinting: el proyecto se basa en interoperabilidad por comportamiento observable. Imprimir implica riesgos físicos, así que prueba siempre con la impresora a la vista.
