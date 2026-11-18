// SPDX-License-Identifier: MIT
//
// threew.h — codificador/decodificador del formato .3w (da Vinci 1.0 Pro).
//
// RECONSTRUIDO a partir de XYZ3W_64.dll (SHA-256 fde307f9…ab452).
// No es código fuente de XYZprinting. Ver docs/formato-3w.md.

#pragma once

#include <QByteArray>
#include <QString>

namespace threew {

inline constexpr int kHeaderSize = 0x2000;

// Clave por defecto de la ruta FPro (32 bytes ASCII).
QByteArray defaultKey();

// Construye un .3w a partir del G-code en claro.
// Devuelve un QByteArray vacío y rellena `error` si falla.
QByteArray encode(const QByteArray &gcode,
                  const QByteArray &key = defaultKey(),
                  QString *error = nullptr);

// Convierte CRLF/CR/LF a CRLF, como el G-code de un .3w real.
inline QByteArray toCrlf(QByteArray gcode)
{
    gcode.replace("\r\n", "\n");
    gcode.replace('\r', '\n');
    gcode.replace('\n', "\r\n");
    return gcode;
}

struct Decoded {
    QByteArray gcode;
    quint32 textLength = 0;
    quint32 field28 = 0;
    quint32 field2c = 0;
    quint32 crc32 = 0;
    bool crcOk = false;
    QString keyTag;
    QString headerText;
};

bool decode(const QByteArray &file,
            const QByteArray &key,
            Decoded *out,
            QString *error = nullptr);

} // namespace threew
