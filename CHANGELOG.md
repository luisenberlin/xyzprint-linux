# Registro de cambios

El formato sigue [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/) y el proyecto usa [versionado semántico](https://semver.org/lang/es/).

## [Sin publicar]

### Añadido
- Integración continua (compilación y tests) y workflow de publicación de releases.
- `.gitignore`, `CONTRIBUTING.md`, `CHANGELOG.md` y README en inglés.
- Tabla «Probado en» y sección sobre la regla udev en el README.

### Corregido
- Los scripts de `tools/` no tenían permiso de ejecución en el repositorio (`./tools/install-local.sh` fallaba tras clonar).
- El paquete `.tar.gz` ahora incluye la regla udev.

### Cambiado
- `how-to.md` pasa a `docs/how-to.md`.
- Se elimina `LEEME.txt` (duplicaba la documentación de `docs/` y del README).

## [0.1.0]

### Añadido
- CLI `xyzprint-cli`: `ports`, `status`, `query`, `pause`, `resume`, `cancel`, `complete`, `upload`, `send`, `wrap`, `encode`, `decode`, `inspect` y `slice`.
- Protocolo USB serie y formato `.3w` reconstruidos y validados contra una da Vinci 1.0 Pro real.
- Laminado con CuraEngine y perfil de la 1.0 Pro (PLA, boquilla de 0,4 mm).
- `xyzsend`, instalador local, menú contextual de Caja y empaquetado en `.tar.gz`.
