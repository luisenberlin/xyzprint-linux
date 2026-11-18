// SPDX-License-Identifier: MIT
//
// threew.cpp — implementación del formato .3w (ruta FPro, da Vinci 1.0 Pro).
//
// RECONSTRUIDO a partir de XYZ3W_64.dll. Validado byte a byte contra ficheros
// generados por el DLL original (ver docs/formato-3w.md).

#include "threew.h"

#include <openssl/evp.h>

#include <array>
#include <cstring>

namespace threew {
namespace {

constexpr char kMagic[12] = {'3', 'D', 'P', 'F', 'N', 'K', 'G', '1', '3', 'W', 'T', 'W'};

const std::array<quint32, 256> &crcTable()
{
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> t{};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();
    return table;
}

quint32 crc32(const QByteArray &data)
{
    quint32 crc = 0xFFFFFFFFu;
    const auto &t = crcTable();
    for (unsigned char b : data)
        crc = t[(crc ^ b) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

void putBe32(QByteArray &buf, int offset, quint32 value)
{
    buf[offset + 0] = char((value >> 24) & 0xFF);
    buf[offset + 1] = char((value >> 16) & 0xFF);
    buf[offset + 2] = char((value >> 8) & 0xFF);
    buf[offset + 3] = char(value & 0xFF);
}

quint32 getBe32(const QByteArray &buf, int offset)
{
    return (quint32(quint8(buf[offset + 0])) << 24)
         | (quint32(quint8(buf[offset + 1])) << 16)
         | (quint32(quint8(buf[offset + 2])) << 8)
         |  quint32(quint8(buf[offset + 3]));
}

// AES-ECB sin relleno; `encrypt` selecciona dirección.
QByteArray aesEcb(const QByteArray &input, const QByteArray &key, bool encrypt)
{
    if (key.size() != 16 && key.size() != 32)
        return {};
    if (input.size() % 16 != 0)
        return {};

    const EVP_CIPHER *cipher = key.size() == 32 ? EVP_aes_256_ecb() : EVP_aes_128_ecb();
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return {};

    QByteArray output(input.size() + 16, Qt::Uninitialized);
    int outLen = 0;
    int finalLen = 0;
    bool ok = false;

    if (encrypt)
        ok = EVP_EncryptInit_ex(ctx, cipher, nullptr,
                                reinterpret_cast<const unsigned char *>(key.constData()), nullptr) == 1;
    else
        ok = EVP_DecryptInit_ex(ctx, cipher, nullptr,
                                reinterpret_cast<const unsigned char *>(key.constData()), nullptr) == 1;

    if (ok) {
        EVP_CIPHER_CTX_set_padding(ctx, 0);
        ok = (encrypt ? EVP_EncryptUpdate : EVP_DecryptUpdate)(
                 ctx,
                 reinterpret_cast<unsigned char *>(output.data()), &outLen,
                 reinterpret_cast<const unsigned char *>(input.constData()), input.size()) == 1;
    }
    if (ok) {
        ok = (encrypt ? EVP_EncryptFinal_ex : EVP_DecryptFinal_ex)(
                 ctx,
                 reinterpret_cast<unsigned char *>(output.data()) + outLen, &finalLen) == 1;
    }
    EVP_CIPHER_CTX_free(ctx);

    if (!ok)
        return {};
    output.resize(outLen + finalLen);
    return output;
}

} // namespace

QByteArray defaultKey()
{
    return QByteArrayLiteral("@xyzprinting.com@xyzprinting.com");
}

QByteArray encode(const QByteArray &gcode, const QByteArray &key, QString *error)
{
    const auto fail = [error](const QString &msg) {
        if (error) *error = msg;
        return QByteArray();
    };

    if (key.size() != 16 && key.size() != 32)
        return fail(QStringLiteral("clave de %1 bytes: se esperaban 16 o 32").arg(key.size()));

    const int g21 = gcode.indexOf("G21 ;");
    if (g21 < 0)
        return fail(QStringLiteral("no se encontró \"G21 ;\" en el G-code"));
    if (g21 > kHeaderSize - 0x71)
        return fail(QStringLiteral("prefijo de cabecera demasiado largo: %1 B").arg(g21));

    // ceil(n/16)*16; si n ya es múltiplo de 16 no se añade bloque extra.
    const int encLen = ((gcode.size() + 15) / 16) * 16;
    QByteArray padded(encLen, '\0');
    std::memcpy(padded.data(), gcode.constData(), size_t(gcode.size()));
    const int padLen = encLen - gcode.size();
    if (padLen > 0)
        std::memset(padded.data() + gcode.size(), padLen, size_t(padLen));

    const QByteArray payload = aesEcb(padded, key, true);
    if (payload.isEmpty())
        return fail(QStringLiteral("fallo de cifrado AES"));

    QByteArray header(kHeaderSize, '\0');
    std::memcpy(header.data(), kMagic, sizeof(kMagic));
    header[0x0C] = 0x01; // versión 1.5
    header[0x0D] = 0x05;
    putBe32(header, 0x10, 8);
    std::memcpy(header.data() + 0x1C, "TagE", 4);
    std::memcpy(header.data() + 0x20, key.size() == 16 ? "a128" : "J256", 4);
    putBe32(header, 0x24, quint32(g21));
    putBe32(header, 0x28, 0x44);
    putBe32(header, 0x2C, 1);
    putBe32(header, 0x30, crc32(payload));
    std::memcpy(header.data() + 0x70, gcode.constData(), size_t(g21));
    header[0x70 + g21] = '\0';

    return header + payload;
}

bool decode(const QByteArray &file, const QByteArray &key, Decoded *out, QString *error)
{
    const auto fail = [error](const QString &msg) {
        if (error) *error = msg;
        return false;
    };

    if (!out)
        return fail(QStringLiteral("salida nula"));
    if (file.size() < kHeaderSize)
        return fail(QStringLiteral("fichero menor que la cabecera"));
    if (std::memcmp(file.constData(), kMagic, sizeof(kMagic)) != 0)
        return fail(QStringLiteral("magic .3w incorrecto"));

    const QByteArray tag = file.mid(0x20, 4);
    const int expected = tag == "J256" ? 32 : tag == "a128" ? 16 : 0;
    if (expected == 0)
        return fail(QStringLiteral("tag de clave desconocido: %1").arg(QString::fromLatin1(tag)));
    if (key.size() != expected)
        return fail(QStringLiteral("tag %1 exige clave de %2 bytes, recibida de %3")
                        .arg(QString::fromLatin1(tag)).arg(expected).arg(key.size()));

    const QByteArray payload = file.mid(kHeaderSize);
    if (payload.isEmpty() || payload.size() % 16 != 0)
        return fail(QStringLiteral("longitud de payload inválida: %1").arg(payload.size()));

    QByteArray plain = aesEcb(payload, key, false);
    if (plain.isEmpty())
        return fail(QStringLiteral("fallo de descifrado AES"));

    // El DLL recorta el relleno solo si el último byte está en 2..16;
    // un relleno de 1 byte se deja tal cual (bug observado).
    const int pad = quint8(plain.at(plain.size() - 1));
    if (pad >= 2 && pad <= 16 && pad <= plain.size()) {
        bool consistent = true;
        for (int i = plain.size() - pad; i < plain.size(); ++i) {
            if (quint8(plain.at(i)) != pad) { consistent = false; break; }
        }
        if (consistent)
            plain.chop(pad);
    }

    out->gcode = plain;
    out->textLength = getBe32(file, 0x24);
    out->field28 = getBe32(file, 0x28);
    out->field2c = getBe32(file, 0x2C);
    out->crc32 = getBe32(file, 0x30);
    out->crcOk = (crc32(payload) == out->crc32);
    out->keyTag = QString::fromLatin1(tag);
    out->headerText = QString::fromLatin1(
        file.mid(0x70, int(qMin<quint32>(out->textLength, quint32(kHeaderSize - 0x70)))));
    return true;
}

} // namespace threew
