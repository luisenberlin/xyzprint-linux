// SPDX-License-Identifier: MIT
//
// xyzserial.cpp — protocolo serie de la da Vinci 1.0 Pro.
//
// RECONSTRUIDO a partir de XYZcontroller.dll. No es código fuente de XYZprinting.

#include "xyzserial.h"

#include <QElapsedTimer>
#include <QList>
#include <QThread>

#include <array>
#include <cstring>

namespace xyz {
namespace {

void putBe32(char *dst, quint32 v)
{
    dst[0] = char((v >> 24) & 0xFF);
    dst[1] = char((v >> 16) & 0xFF);
    dst[2] = char((v >> 8) & 0xFF);
    dst[3] = char(v & 0xFF);
}

} // namespace

TagInfo parseTagInfo(const QByteArray &raw)
{
    TagInfo info;
    info.raw = raw;
    const QList<QByteArray> lines = raw.split('\n');
    for (QByteArray line : lines) {
        line = line.trimmed();
        if (line.isEmpty() || line == "$")
            continue;
        const int colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        info.fields.insert(QString::fromLatin1(line.left(colon)),
                           QString::fromUtf8(line.mid(colon + 1)));
    }
    return info;
}

Serial::~Serial()
{
    close();
}

bool Serial::open(const QString &portName, QString *error)
{
    if (m_port.isOpen())
        m_port.close();

    m_port.setPortName(portName);
    m_port.setBaudRate(115200);
    m_port.setDataBits(QSerialPort::Data8);
    m_port.setParity(QSerialPort::NoParity);
    m_port.setStopBits(QSerialPort::OneStop);
    m_port.setFlowControl(QSerialPort::NoFlowControl);

    if (!m_port.open(QIODevice::ReadWrite)) {
        if (error)
            *error = QStringLiteral("no se pudo abrir %1: %2").arg(portName, m_port.errorString());
        return false;
    }

    m_port.setDataTerminalReady(true);
    m_port.setRequestToSend(true);
    // El CDC necesita un margen tras afirmar DTR/RTS; la referencia PowerShell
    // validada contra la impresora usa 250 ms.
    QThread::msleep(250);
    m_port.clear();
    m_rx.clear();
    return true;
}

void Serial::close()
{
    if (m_port.isOpen())
        m_port.close();
    m_rx.clear();
}

bool Serial::isOpen() const
{
    return m_port.isOpen();
}

bool Serial::writeAll(const QByteArray &data, QString *error)
{
    if (!m_port.isOpen()) {
        if (error) *error = QStringLiteral("puerto cerrado");
        return false;
    }
    qint64 written = 0;
    while (written < data.size()) {
        const qint64 n = m_port.write(data.constData() + written, data.size() - written);
        if (n <= 0) {
            if (error) *error = QStringLiteral("fallo de escritura: %1").arg(m_port.errorString());
            return false;
        }
        written += n;
    }
    if (!m_port.waitForBytesWritten(2000)) {
        if (error) *error = QStringLiteral("timeout de escritura");
        return false;
    }
    return true;
}

bool Serial::waitAck(int timeoutMs, QString *error)
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs) {
        m_rx += m_port.readAll();
        const QByteArray reply = m_rx.trimmed();
        if (reply.startsWith("ok"))
            return true;
        if (reply.startsWith("E0")) {
            if (error) *error = QStringLiteral("la impresora abortó (E0)");
            return false;
        }
        if (reply.startsWith("E35"))
            return true;
        if (reply == "no" || reply.startsWith("no\n") || reply.startsWith("no\r")) {
            if (error) *error = QStringLiteral("la impresora respondió no");
            return false;
        }
        m_port.waitForReadyRead(100);
    }

    if (error) *error = QStringLiteral("sin ACK tras %1 ms").arg(timeoutMs);
    return false;
}

QByteArray Serial::query(const QByteArray &what, int timeoutMs)
{
    m_rx.clear();
    m_port.clear(QSerialPort::Input);
    if (!writeAll("XYZv3/query=" + what))
        return {};

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        m_rx += m_port.readAll();
        if (m_rx.endsWith("$\n") || m_rx.endsWith("$\r\n"))
            break;
        m_port.waitForReadyRead(100);
    }
    return m_rx;
}

QByteArray Serial::tagInfo(int timeoutMs)
{
    return query("taginfo", timeoutMs);
}

bool Serial::config(const QByteArray &value, QString *error)
{
    m_rx.clear();
    m_port.clear(QSerialPort::Input);
    return writeAll("XYZv3/config=" + value, error);
}

bool Serial::cancel(QString *error)
{
    return config("print:[cancel]", error);
}

bool Serial::pause(QString *error)
{
    return config("print:[pause]", error);
}

bool Serial::resume(QString *error)
{
    return config("print:[resume]", error);
}

bool Serial::complete(QString *error)
{
    return config("print:[complete]", error);
}

quint32 Serial::crc32(const QByteArray &data)
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

    quint32 crc = 0xFFFFFFFFu;
    for (unsigned char b : data)
        crc = table[(crc ^ b) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

bool Serial::upload(const QByteArray &file,
                    const QString &fileName,
                    const ProgressFn &progress,
                    int chunkSize,
                    QString *error)
{
    if (!m_port.isOpen()) {
        if (error) *error = QStringLiteral("puerto cerrado");
        return false;
    }
    if (file.isEmpty()) {
        if (error) *error = QStringLiteral("fichero vacío");
        return false;
    }
    if (chunkSize <= 0)
        chunkSize = 8192;

    m_rx.clear();
    m_port.clear(QSerialPort::Input);

    // 1. Cabecera de subida (sin terminador). El DLL reintenta hasta 10 veces.
    const QByteArray head = "XYZv3/upload=" + fileName.toLatin1() + ","
                          + QByteArray::number(file.size());
    bool headAcked = false;
    QString headError;
    for (int attempt = 0; attempt < 10 && !headAcked; ++attempt) {
        m_rx.clear();
        m_port.clear(QSerialPort::Input);
        if (!writeAll(head, &headError))
            break;
        headAcked = waitAck(5000, &headError);
        if (headError.startsWith(QStringLiteral("la impresora abortó")))
            break;
    }
    if (!headAcked) {
        if (error) *error = QStringLiteral("cabecera de subida: %1").arg(headError);
        return false;
    }

    // 2. Paquetes: [índice BE32][longitud BE32][payload][CRC-32 BE32].
    int offset = 0;
    quint32 index = 0;
    while (offset < file.size()) {
        const int len = int(qMin<qsizetype>(qsizetype(chunkSize), file.size() - offset));
        const QByteArray payload = file.mid(offset, len);

        QByteArray packet(len + 12, Qt::Uninitialized);
        putBe32(packet.data() + 0, index);
        putBe32(packet.data() + 4, quint32(len));
        std::memcpy(packet.data() + 8, payload.constData(), size_t(len));
        putBe32(packet.data() + len + 8, crc32(payload));

        bool acked = false;
        QString lastError;
        for (int attempt = 0; attempt < 3 && !acked; ++attempt) {
            m_rx.clear();
            m_port.clear(QSerialPort::Input);
            if (!writeAll(packet, &lastError))
                break;
            acked = waitAck(5000, &lastError);
            if (lastError.startsWith(QStringLiteral("la impresora abortó")))
                break;
        }
        if (!acked) {
            if (error) *error = QStringLiteral("paquete %1: %2").arg(index).arg(lastError);
            return false;
        }

        offset += len;
        ++index;
        if (progress)
            progress(offset, int(file.size()));
    }

    // 3. Fin: la impresora arranca la impresión.
    if (!writeAll("XYZv3/uploadDidFinish", error))
        return false;
    QThread::msleep(500);
    return true;
}

} // namespace xyz
