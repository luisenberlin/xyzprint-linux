# Registro de cambios

El formato sigue [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/) y el proyecto usa [versionado semántico](https://semver.org/lang/es/).

## [Sin publicar]

### Añadido
- Captura del diálogo de Caja en los README en español e inglés.

### Corregido
- `tools/xyzsend`: la variable `LD_LIBRARY_PATH` se declara y se asigna por separado (aviso SC2155 de shellcheck). No cambia el comportamiento.

## [0.1.0] - 2026-11-19

Primera versión publicada.

### Añadido
- CLI `xyzprint-cli`: `ports`, `status`, `query`, `pause`, `resume`, `cancel`, `complete`, `upload`, `send`, `wrap`, `encode`, `decode`, `inspect` y `slice`.
- Protocolo USB serie y formato `.3w` reconstruidos y validados contra una da Vinci 1.0 Pro real.
- Laminado con CuraEngine y perfil de la 1.0 Pro (PLA, boquilla de 0,4 mm).
- `xyzsend`, instalador local, menú contextual de Caja y empaquetado en `.tar.gz` (incluye la regla udev y el README en inglés).
- Integración continua (compilación y tests) y workflow de publicación de releases.
- `.gitignore`, `CONTRIBUTING.md`, `CHANGELOG.md` y README en español e inglés.
- Tabla «Probado en» y sección sobre la regla udev en el README.

### Cambiado
- `how-to.md` pasa a `docs/how-to.md`.
- Se elimina `LEEME.txt` (duplicaba la documentación de `docs/` y del README).

### Corregido
- Los scripts de `tools/` no tenían permiso de ejecución en el repositorio (`./tools/install-local.sh` fallaba tras clonar).

[Sin publicar]: https://github.com/luisenberlin/xyzprint-linux/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/luisenberlin/xyzprint-linux/releases/tag/v0.1.0
