// SPDX-License-Identifier: MIT
//
// xyzserial.h — transporte serie de la da Vinci 1.0 Pro.
//
// Reconstruido por interoperabilidad y verificado contra la impresora real.
// Ver docs/protocolo-serie.md.

#pragma once

#include <QByteArray>
#include <QMap>
#include <QSerialPort>
#include <QString>

#include <functional>

namespace xyz {

// Respuesta de "XYZv3/query=taginfo": líneas "clave:valor" (claves de 1 carácter).
struct TagInfo {
    QMap<QString, QString> fields;
    QByteArray raw;

    QString value(const QString &key) const { return fields.value(key); }
};

TagInfo parseTagInfo(const QByteArray &raw);

class Serial
{
public:
    using ProgressFn = std::function<void(int sent, int total)>;

    Serial() = default;
    ~Serial();

    Serial(const Serial &) = delete;
    Serial &operator=(const Serial &) = delete;

    // 115200 8N1, sin flow control.
    bool open(const QString &portName, QString *error = nullptr);
    void close();
    bool isOpen() const;

    // Envía "XYZv3/query=<what>" y devuelve la respuesta cruda.
    QByteArray query(const QByteArray &what, int timeoutMs = 1500);

    // Estado completo: "XYZv3/query=taginfo".
    QByteArray tagInfo(int timeoutMs = 4000);

    // Envía "XYZv3/config=print:[cancel]".
    bool cancel(QString *error = nullptr);
    // Envía "XYZv3/config=print:[pause]".
    bool pause(QString *error = nullptr);
    // Envía "XYZv3/config=print:[resume]".
    bool resume(QString *error = nullptr);
    // Envía "XYZv3/config=print:[complete]".
    bool complete(QString *error = nullptr);
    // Envía "XYZv3/config=<value>".
    bool config(const QByteArray &value, QString *error = nullptr);

    // Sube un .3w y dispara la impresión (uploadDidFinish).
    // chunkSize = 8192 como XYZprint.
    bool upload(const QByteArray &file,
                const QString &fileName,
                const ProgressFn &progress = {},
                int chunkSize = 8192,
                QString *error = nullptr);

    static quint32 crc32(const QByteArray &data);

private:
    bool writeAll(const QByteArray &data, QString *error = nullptr);
    // true si la impresora responde "ok" (o "E35", que también continúa).
    bool waitAck(int timeoutMs, QString *error);

    QSerialPort m_port;
    QByteArray m_rx;
};

} // namespace xyz
