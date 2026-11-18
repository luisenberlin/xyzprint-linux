# Hoja de ruta

## Hecho

- [x] Protocolo USB serie (115200 8N1, comandos `XYZv3/*`, paquetes de 8192 B).
- [x] Formato `.3w` (cabecera de 8 KB, AES-256-ECB, CRC-32, cabecera de texto).
- [x] CLI en C++/Qt6: `ports`, `status`, `query`, `pause`, `resume`, `cancel`,
      `complete`, `upload`, `send`, `wrap`, `encode`, `decode`, `inspect`, `slice`.
- [x] Laminado con CuraEngine y perfil de la 1.0 Pro.
- [x] Estimación de tiempo y filamento a partir del G-code.
- [x] `xyzsend`: un solo comando para empaquetar y enviar.
- [x] Instalador local (`install-local.sh`) con opción `--bundle` para llevarlo
      a otro equipo sin Qt instalado.
- [x] Menú contextual de Caja.
- [x] Paquete `.tar.gz` reproducible (`make-dist.sh`).
- [x] Impresión real verificada (cubo de 10 mm, 2026-11-18).

## Siguiente

- [ ] **Interfaz gráfica** (Qt6 Widgets): elegir STL, ver la cama, laminar y
      enviar sin tocar la terminal.
- [ ] **AppImage** con `linuxdeploy`, icono, `.desktop` y AppStream.
- [ ] **Plugin de Cura 5.x** (OutputDevice): botón *Imprimir* dentro de Cura.
- [ ] Acción de Caja sobre `.stl`: laminar y enviar en un paso.
- [ ] Perfiles para otros materiales (ABS, PETG) y para 0,3 mm.
- [ ] Más tests: G-code de referencia, subida simulada.
- [ ] Capturas y vídeo para el README.

## Ideas

- Cola de trabajos local (varios `.3w` seguidos, esperando el OK del LCD).
- Notificación de escritorio al terminar la impresión.
- Compatibilidad con otros modelos de la familia da Vinci que usen la ruta
  `a128` del formato `.3w`.
