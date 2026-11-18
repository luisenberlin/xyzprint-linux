// SPDX-License-Identifier: MIT
//
// main.cpp — CLI de referencia para el port Linux de XYZprint.
//
// Reconstruido a partir de XYZcontroller.dll, XYZ3W_64.dll y
// XYZSerialDriver.dll. No es código fuente de XYZprinting.
//
// Subcomandos:
//   ports                          lista puertos serie disponibles
//   status  [--port /dev/ttyACM0]  estado completo (taginfo)
//   query   <clave> [--port ...]   consulta cruda (p. ej. j, w, O)
//   cancel|pause|resume|complete   control de la impresión
//   upload  <fichero.3w> --yes     sube y ARRANCA LA IMPRESIÓN
//   send    <gcode|.3w> --yes      empaqueta si hace falta, sube y ARRANCA
//   slice   <modelo.stl> <out.3w>  lamina con CuraEngine y empaqueta el .3w
//   wrap    <in.gcode> <out.3w>    antepone la cabecera daVinci y empaqueta
//   encode  <in.gcode> <out.3w>    construye un .3w (el G-code ya trae cabecera)
//   decode  <in.3w> <out.gcode>    extrae el G-code
//   inspect <in.3w>                muestra la cabecera
//
// El puerto por defecto es /dev/ttyACM0 (Linux); en Windows, COM3.

#include "slicer.h"
#include "threew.h"
#include "xyzserial.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QSerialPortInfo>
#include <QTextStream>

#include <cstdio>

namespace {

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

QTextStream &err()
{
    static QTextStream stream(stderr);
    return stream;
}

int fail(const QString &message)
{
    err() << "error: " << message << Qt::endl;
    return 1;
}

// Códigos de `j` observados en vivo (ver docs/protocolo-serie.md §2.2).
QString estadoLegible(const QString &codigo)
{
    if (codigo == QStringLiteral("9511"))
        return QStringLiteral("9511 (en reposo)");
    if (codigo == QStringLiteral("9500"))
        return QStringLiteral("9500 (preparando la impresión)");
    if (codigo == QStringLiteral("9501"))
        return QStringLiteral("9501 (calentando)");
    if (codigo == QStringLiteral("9505"))
        return QStringLiteral("9505 (imprimiendo)");
    if (codigo == QStringLiteral("9509"))
        return QStringLiteral("9509 (trabajo terminado; pulsa OK en el LCD)");
    return codigo;
}

bool readFile(const QString &path, QByteArray *data, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("no se pudo leer %1: %2").arg(path, file.errorString());
        return false;
    }
    *data = file.readAll();
    if (data->isEmpty()) {
        *error = QStringLiteral("fichero vacío: %1").arg(path);
        return false;
    }
    return true;
}

bool writeFile(const QString &path, const QByteArray &data, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = QStringLiteral("no se pudo escribir %1: %2").arg(path, file.errorString());
        return false;
    }
    if (file.write(data) != data.size()) {
        *error = QStringLiteral("escritura incompleta en %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

QString printable(const QByteArray &data)
{
    QString text;
    text.reserve(data.size());
    for (unsigned char c : data)
        text += (c >= 32 && c < 127) ? QLatin1Char(char(c)) : QLatin1Char('.');
    return text;
}

QString formatDuration(int seconds)
{
    if (seconds < 0)
        return QStringLiteral("desconocido");
    const int hours = seconds / 3600;
    const int minutes = (seconds % 3600) / 60;
    const int remaining = seconds % 60;
    if (hours > 0)
        return QStringLiteral("%1 h %2 min").arg(hours).arg(minutes);
    if (minutes > 0)
        return QStringLiteral("%1 min %2 s").arg(minutes).arg(remaining);
    return QStringLiteral("%1 s").arg(remaining);
}

int cmdPorts()
{
    const auto ports = QSerialPortInfo::availablePorts();
    if (ports.isEmpty()) {
        out() << "no se encontró ningún puerto serie" << Qt::endl;
        return 0;
    }
    for (const QSerialPortInfo &info : ports) {
        out() << info.portName();
        if (!info.description().isEmpty())
            out() << "  " << info.description();
        if (info.hasVendorIdentifier() && info.hasProductIdentifier()) {
            out() << QStringLiteral("  [%1:%2]")
                         .arg(info.vendorIdentifier(), 4, 16, QLatin1Char('0'))
                         .arg(info.productIdentifier(), 4, 16, QLatin1Char('0'));
        }
        out() << Qt::endl;
    }
    return 0;
}

int cmdStatus(const QString &portName)
{
    xyz::Serial serial;
    QString error;
    if (!serial.open(portName, &error))
        return fail(error);

    const QByteArray raw = serial.tagInfo();
    if (raw.isEmpty())
        return fail(QStringLiteral("sin respuesta a taginfo"));

    const xyz::TagInfo info = xyz::parseTagInfo(raw);
    if (info.fields.isEmpty()) {
        out() << printable(raw) << Qt::endl;
        return 0;
    }

    const auto show = [&info](const char *key, const char *label) {
        const QString value = info.value(QString::fromLatin1(key));
        if (!value.isEmpty())
            out() << QStringLiteral("%1: %2").arg(QString::fromUtf8(label), value) << Qt::endl;
    };
    show("n", "nombre");
    show("i", "serie");
    show("p", "modelo");
    show("v", "firmware");
    show("V", "firmware UI");
    const QString estado = info.value(QStringLiteral("j"));
    if (!estado.isEmpty())
        out() << QStringLiteral("estado: %1").arg(estadoLegible(estado)) << Qt::endl;
    show("O", "temperaturas");
    show("w", "cartucho");
    show("L", "trabajo");
    show("s", "flags");
    show("4", "red");

    out() << "--- campos ---" << Qt::endl;
    for (auto it = info.fields.cbegin(); it != info.fields.cend(); ++it)
        out() << it.key() << ": " << it.value() << Qt::endl;
    return 0;
}

int cmdQuery(const QString &portName, const QString &what)
{
    xyz::Serial serial;
    QString error;
    if (!serial.open(portName, &error))
        return fail(error);

    const QByteArray reply = serial.query(what.toLatin1());
    out() << printable(reply) << Qt::endl;
    if (reply.isEmpty())
        return fail(QStringLiteral("sin respuesta"));
    return 0;
}

int cmdControl(const QString &portName, const QString &what)
{
    xyz::Serial serial;
    QString error;
    if (!serial.open(portName, &error))
        return fail(error);

    bool ok = false;
    if (what == QStringLiteral("cancel"))
        ok = serial.cancel(&error);
    else if (what == QStringLiteral("pause"))
        ok = serial.pause(&error);
    else if (what == QStringLiteral("resume"))
        ok = serial.resume(&error);
    else if (what == QStringLiteral("complete"))
        ok = serial.complete(&error);
    else
        return fail(QStringLiteral("control desconocido: %1").arg(what));

    if (!ok)
        return fail(error);
    out() << QStringLiteral("comando %1 enviado").arg(what) << Qt::endl;
    return 0;
}

// Cabecera que espera la 1.0 Pro. Cura/PrusaSlicer no la emiten: sin
// "; machine = daVinciAW10" y el "; print_time" la impresora pierde la
// estimación de tiempo restante. Se completan solo los campos deducibles del
// propio G-code.
QByteArray daVinciHeader(const QByteArray &gcode, const QString &name)
{
    const auto firstMatch = [&gcode](const QList<QByteArray> &needles) -> QByteArray {
        for (const QByteArray &needle : needles) {
            const int at = gcode.indexOf(needle);
            if (at < 0)
                continue;
            int end = gcode.indexOf('\n', at);
            if (end < 0)
                end = gcode.size();
            return gcode.mid(at + needle.size(), end - at - needle.size()).trimmed();
        }
        return QByteArray();
    };
    const auto firstNumber = [](const QByteArray &raw) -> QByteArray {
        QByteArray out;
        for (const char c : raw) {
            if ((c >= '0' && c <= '9') || c == '.' || c == '-')
                out.append(c);
            else if (!out.isEmpty())
                break;
        }
        return out;
    };

    QByteArray header = "; filename = " + name.toLatin1() + "\n";

    QByteArray printTime = firstNumber(firstMatch({";TIME:", "; print_time ="}));
    if (!printTime.isEmpty())
        header += "; print_time = " + printTime + "\n";

    header += "; machine = daVinciAW10\n";

    int layers = firstNumber(firstMatch({";LAYER_COUNT:", "; total_layers ="})).toInt();
    if (layers <= 0) {
        layers = 0;
        int from = 0;
        while ((from = gcode.indexOf(";LAYER:", from)) >= 0) {
            ++layers;
            from += 7;
        }
    }
    if (layers > 0)
        header += "; total_layers = " + QByteArray::number(layers) + "\n";

    const QByteArray nozzle = firstNumber(firstMatch({";Nozzle diameter:", "; nozzle_diameter ="}));
    header += "; nozzle_diameter = " + (nozzle.isEmpty() ? QByteArray("0.40") : nozzle) + "\n";
    const QByteArray temp = firstNumber(firstMatch({"M104 S", "M109 S", "; temperature ="}));
    if (!temp.isEmpty())
        header += "; temperature = " + temp + "\n";
    const QByteArray bed = firstNumber(firstMatch({"M140 S", "M190 S", "; bed_temperature ="}));
    if (!bed.isEmpty())
        header += "; bed_temperature = " + bed + "\n";
    const QByteArray layerHeight = firstNumber(firstMatch({";Layer height:", "; layer_height ="}));
    if (!layerHeight.isEmpty())
        header += "; layer_height = " + layerHeight + "\n";
    const QByteArray filament = firstNumber(firstMatch({";Filament used:", "; total_filament ="}));
    if (!filament.isEmpty() && filament != "0")
        header += "; total_filament = " + filament + "\n";
    header += "; generated_by = xyzprint-cli send\n";
    return header;
}

// Sube un .3w ya empaquetado. Devuelve 0 si la impresora arrancó.
int uploadFile(const QString &portName, const QByteArray &data,
               const QString &name, int chunkSize)
{
    xyz::Serial serial;
    QString error;
    if (!serial.open(portName, &error))
        return fail(error);

    out() << QStringLiteral("subiendo %1 (%2 B, chunks de %3 B) por %4")
                 .arg(name).arg(data.size()).arg(chunkSize).arg(portName)
          << Qt::endl;

    int lastPercent = -1;
    const bool ok = serial.upload(
        data, name,
        [&lastPercent](int sent, int total) {
            const int percent = total > 0 ? int(100LL * sent / total) : 100;
            if (percent != lastPercent) {
                lastPercent = percent;
                err() << QStringLiteral("\r%1%").arg(percent, 3) << Qt::flush;
            }
        },
        chunkSize, &error);

    err() << Qt::endl;
    if (!ok)
        return fail(error);

    out() << "subida completada; la impresora debería haber arrancado" << Qt::endl;
    return 0;
}

int cmdUpload(const QString &portName, const QString &path, int chunkSize, bool confirmed)
{
    if (!confirmed) {
        return fail(QStringLiteral(
            "la impresora ARRANCA LA IMPRESIÓN al terminar la subida; "
            "repite con --yes si es lo que quieres"));
    }

    QByteArray data;
    QString error;
    if (!readFile(path, &data, &error))
        return fail(error);

    return uploadFile(portName, data, QFileInfo(path).fileName(), chunkSize);
}

// send <gcode|.3w> --yes: empaqueta si el fichero no es ya un .3w y lo sube.
int cmdSend(const QString &portName, const QString &input, const QString &output,
            const QString &name, int chunkSize, bool confirmed, const QByteArray &key)
{
    if (!confirmed) {
        return fail(QStringLiteral(
            "la impresora ARRANCA LA IMPRESIÓN al terminar la subida; "
            "repite con --yes si es lo que quieres"));
    }

    const QFileInfo info(input);
    if (!info.exists())
        return fail(QStringLiteral("no existe el fichero: %1").arg(input));

    const bool isThreeW = info.suffix().compare(QStringLiteral("3w"), Qt::CaseInsensitive) == 0;
    if (isThreeW) {
        QByteArray data;
        QString error;
        if (!readFile(input, &data, &error))
            return fail(error);
        const QString uploadName = name.isEmpty() ? info.fileName() : name;
        return uploadFile(portName, data, uploadName, chunkSize);
    }

    QByteArray gcode;
    QString error;
    if (!readFile(input, &gcode, &error))
        return fail(error);
    if (gcode.isEmpty())
        return fail(QStringLiteral("el G-code está vacío: %1").arg(input));

    // La 1.0 Pro no arranca sin la secuencia de precalentamiento. Avisar (no
    // abortar) si el G-code viene de un perfil genérico sin M108/M191.
    if (!gcode.contains("M108") || !gcode.contains("M191")) {
        err() << "aviso: el G-code no contiene M108/M191; si la impresora no "
                 "arranca, revisa el Start G-code del laminador" << Qt::endl;
    }

    const QString uploadName = name.isEmpty()
        ? info.completeBaseName() + QStringLiteral(".gcode")
        : name;

    const QByteArray marker = "; machine = daVinciAW10";
    if (!gcode.left(threew::kHeaderSize).contains(marker))
        gcode.prepend(daVinciHeader(gcode, uploadName));
    gcode = threew::toCrlf(gcode);

    const QByteArray file = threew::encode(gcode, key, &error);
    if (file.isEmpty())
        return fail(error);

    QString outPath = output;
    if (outPath.isEmpty())
        outPath = info.absolutePath() + QLatin1Char('/')
                + info.completeBaseName() + QStringLiteral(".3w");
    if (!writeFile(outPath, file, &error))
        return fail(error);

    out() << QStringLiteral("%1: %2 B de G-code -> %3 B (.3w)")
                 .arg(outPath).arg(gcode.size()).arg(file.size())
          << Qt::endl;

    return uploadFile(portName, file, uploadName, chunkSize);
}

int cmdWrap(const QString &input, const QString &output, const QString &name, const QByteArray &key)
{
    QByteArray gcode;
    QString error;
    if (!readFile(input, &gcode, &error))
        return fail(error);

    // Cura/PrusaSlicer no emiten la cabecera que espera la 1.0 Pro. Se antepone
    // solo si el G-code no la trae ya (para no duplicarla en los vectores).
    const QByteArray marker = "; machine = daVinciAW10";
    if (!gcode.left(threew::kHeaderSize).contains(marker))
        gcode.prepend(daVinciHeader(gcode, name));


    // El .3w real usa CRLF; normalizar evita depender de cómo lo escribió el
    // laminador de origen.
    gcode = threew::toCrlf(gcode);

    const QByteArray file = threew::encode(gcode, key, &error);
    if (file.isEmpty())
        return fail(error);
    if (!writeFile(output, file, &error))
        return fail(error);

    out() << QStringLiteral("%1: %2 B de G-code -> %3 B (.3w)")
                 .arg(output).arg(gcode.size()).arg(file.size())
          << Qt::endl;
    return 0;
}

int cmdSlice(const QString &model, const QString &output, const QString &profile,
             const QString &cura, const QString &definitions,
             const QStringList &overrides, const QString &name,
             const QString &keepGcode, const QByteArray &key)
{
    xyz::SliceRequest request;
    request.modelPath = model;
    request.profilePath = profile;
    request.curaEngine = cura;
    request.definitionsDir = definitions;
    request.overrides = overrides;
    request.name = name;

    xyz::SliceResult result;
    QString error;
    if (!xyz::slice(request, &result, &error))
        return fail(error);

    const QByteArray file = threew::encode(result.gcode, key, &error);
    if (file.isEmpty())
        return fail(error);
    if (!writeFile(output, file, &error))
        return fail(error);
    if (!keepGcode.isEmpty() && !writeFile(keepGcode, result.gcode, &error))
        return fail(error);

    out() << QStringLiteral("perfil      : %1").arg(result.profileName) << Qt::endl;
    if (result.layers > 0)
        out() << QStringLiteral("capas       : %1").arg(result.layers) << Qt::endl;
    if (result.estimatedSeconds >= 0)
        out() << QStringLiteral("tiempo est. : %1")
                     .arg(formatDuration(result.estimatedSeconds)) << Qt::endl;
    if (result.filamentMeters >= 0.0)
        out() << QStringLiteral("filamento   : %1 m")
                     .arg(result.filamentMeters, 0, 'f', 2) << Qt::endl;
    if (!keepGcode.isEmpty())
        out() << QStringLiteral("g-code      : %1").arg(keepGcode) << Qt::endl;
    out() << QStringLiteral("%1: %2 B de G-code -> %3 B (.3w)")
                 .arg(output).arg(result.gcode.size()).arg(file.size())
          << Qt::endl;
    return 0;
}

int cmdEncode(const QString &input, const QString &output, const QByteArray &key)
{
    QByteArray gcode;
    QString error;
    if (!readFile(input, &gcode, &error))
        return fail(error);

    const QByteArray file = threew::encode(gcode, key, &error);
    if (file.isEmpty())
        return fail(error);
    if (!writeFile(output, file, &error))
        return fail(error);

    out() << QStringLiteral("%1: %2 B de G-code -> %3 B")
                 .arg(output).arg(gcode.size()).arg(file.size())
          << Qt::endl;
    return 0;
}

int cmdDecode(const QString &input, const QString &output, const QByteArray &key)
{
    QByteArray file;
    QString error;
    if (!readFile(input, &file, &error))
        return fail(error);

    threew::Decoded decoded;
    if (!threew::decode(file, key, &decoded, &error))
        return fail(error);
    if (!writeFile(output, decoded.gcode, &error))
        return fail(error);

    out() << QStringLiteral("%1: %2 B  crc32_ok=%3  text_len=%4  tag=%5")
                 .arg(output)
                 .arg(decoded.gcode.size())
                 .arg(decoded.crcOk ? QStringLiteral("sí") : QStringLiteral("no"))
                 .arg(decoded.textLength)
                 .arg(decoded.keyTag)
          << Qt::endl;
    return 0;
}

int cmdInspect(const QString &input, const QByteArray &key)
{
    QByteArray file;
    QString error;
    if (!readFile(input, &file, &error))
        return fail(error);

    threew::Decoded decoded;
    if (!threew::decode(file, key, &decoded, &error))
        return fail(error);

    out() << QStringLiteral("fichero      : %1 (%2 B)").arg(input).arg(file.size()) << Qt::endl
          << QStringLiteral("tag de clave : %1").arg(decoded.keyTag) << Qt::endl
          << QStringLiteral("text_len     : %1").arg(decoded.textLength) << Qt::endl
          << QStringLiteral("campo 0x28   : 0x%1").arg(decoded.field28, 8, 16, QLatin1Char('0')) << Qt::endl
          << QStringLiteral("campo 0x2C   : %1").arg(decoded.field2c) << Qt::endl
          << QStringLiteral("crc32        : 0x%1 (%2)")
                 .arg(decoded.crc32, 8, 16, QLatin1Char('0'))
                 .arg(decoded.crcOk ? QStringLiteral("válido") : QStringLiteral("NO coincide"))
          << Qt::endl
          << QStringLiteral("g-code       : %1 B").arg(decoded.gcode.size()) << Qt::endl
          << QStringLiteral("prefijo      : %1").arg(decoded.headerText.trimmed()) << Qt::endl;
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("xyzprint-cli"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "CLI reconstruida para la XYZprinting da Vinci 1.0 Pro (USB serie)."));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption portOption(
        {QStringLiteral("p"), QStringLiteral("port")},
        QStringLiteral("Puerto serie (por defecto /dev/ttyACM0)."),
        QStringLiteral("puerto"), QStringLiteral("/dev/ttyACM0"));
    QCommandLineOption keyOption(
        {QStringLiteral("k"), QStringLiteral("key")},
        QStringLiteral("Clave AES del .3w (32 bytes ASCII; por defecto la de fábrica)."),
        QStringLiteral("clave"));
    QCommandLineOption chunkOption(
        QStringLiteral("chunk"),
        QStringLiteral("Tamaño de paquete en bytes (por defecto 8192)."),
        QStringLiteral("bytes"), QStringLiteral("8192"));
    QCommandLineOption nameOption(
        QStringLiteral("name"),
        QStringLiteral("Nombre que se guarda en la cabecera del .3w (por defecto, el del G-code)."),
        QStringLiteral("fichero"));
    QCommandLineOption profileOption(
        QStringLiteral("profile"),
        QStringLiteral("Perfil JSON de CuraEngine (por defecto profiles/davinci-1.0-pro-pla.json)."),
        QStringLiteral("fichero"));
    QCommandLineOption curaOption(
        QStringLiteral("cura"),
        QStringLiteral("Ruta a CuraEngine (por defecto $CURA_ENGINE o PATH)."),
        QStringLiteral("ruta"));
    QCommandLineOption definitionsOption(
        QStringLiteral("definitions"),
        QStringLiteral("Directorio con fdmprinter.def.json y fdmextruder.def.json."),
        QStringLiteral("directorio"));
    QCommandLineOption setOption(
        QStringLiteral("set"),
        QStringLiteral("Sobrescribe un ajuste (clave=valor; e:clave=valor para el extrusor)."),
        QStringLiteral("clave=valor"));
    QCommandLineOption keepGcodeOption(
        QStringLiteral("keep-gcode"),
        QStringLiteral("Guarda también el G-code intermedio en la ruta indicada."),
        QStringLiteral("fichero"));
    QCommandLineOption outputOption(
        {QStringLiteral("o"), QStringLiteral("output")},
        QStringLiteral("Ruta del .3w que genera 'send' (por defecto, junto al G-code)."),
        QStringLiteral("fichero"));
    QCommandLineOption yesOption(
        {QStringLiteral("y"), QStringLiteral("yes")},
        QStringLiteral("Confirma que se quiere imprimir de verdad."));

    parser.addOption(portOption);
    parser.addOption(keyOption);
    parser.addOption(chunkOption);
    parser.addOption(nameOption);
    parser.addOption(profileOption);
    parser.addOption(curaOption);
    parser.addOption(definitionsOption);
    parser.addOption(setOption);
    parser.addOption(keepGcodeOption);
    parser.addOption(outputOption);
    parser.addOption(yesOption);
    parser.addPositionalArgument(
        QStringLiteral("comando"),
        QStringLiteral("ports | status | query | cancel | pause | resume | complete | upload | send | slice | wrap | encode | decode | inspect"));
    parser.addPositionalArgument(
        QStringLiteral("argumentos"),
        QStringLiteral("query <clave> | upload <fichero.3w> | send <fichero.gcode> | slice <modelo.stl> <salida.3w> | wrap <in.gcode> <out.3w> | encode <in> <out> | decode <in> <out> | inspect <in>"));

    parser.process(app);

    const QStringList args = parser.positionalArguments();
    if (args.isEmpty()) {
        parser.showHelp(2);
        return 2;
    }

    const QString command = args.at(0);
    const QString portName = parser.value(portOption);
    const QByteArray key = parser.isSet(keyOption)
        ? parser.value(keyOption).toLatin1()
        : threew::defaultKey();

    if (command == QStringLiteral("ports"))
        return cmdPorts();
    if (command == QStringLiteral("status"))
        return cmdStatus(portName);
    if (command == QStringLiteral("cancel") || command == QStringLiteral("pause")
        || command == QStringLiteral("resume") || command == QStringLiteral("complete")) {
        return cmdControl(portName, command);
    }

    if (command == QStringLiteral("query")) {
        if (args.size() < 2)
            return fail(QStringLiteral("falta la clave de consulta (j, w, O…)"));
        return cmdQuery(portName, args.at(1));
    }

    if (command == QStringLiteral("upload")) {
        if (args.size() < 2)
            return fail(QStringLiteral("falta el fichero .3w"));
        bool ok = false;
        const int chunkSize = parser.value(chunkOption).toInt(&ok);
        if (!ok || chunkSize <= 0)
            return fail(QStringLiteral("--chunk inválido"));
        return cmdUpload(portName, args.at(1), chunkSize, parser.isSet(yesOption));
    }

    if (command == QStringLiteral("send")) {
        if (args.size() < 2)
            return fail(QStringLiteral(
                "uso: send <fichero.gcode|.3w> --yes [--port /dev/ttyACM0] "
                "[--name nombre.gcode] [--output salida.3w]"));
        bool ok = false;
        const int chunkSize = parser.value(chunkOption).toInt(&ok);
        if (!ok || chunkSize <= 0)
            return fail(QStringLiteral("--chunk inválido"));
        return cmdSend(portName, args.at(1), parser.value(outputOption),
                       parser.isSet(nameOption) ? parser.value(nameOption) : QString(),
                       chunkSize, parser.isSet(yesOption), key);
    }

    if (command == QStringLiteral("slice")) {
        if (args.size() < 3)
            return fail(QStringLiteral(
                "uso: slice <modelo.stl> <salida.3w> [--profile perfil.json] "
                "[--cura CuraEngine] [--definitions dir] [--set clave=valor] "
                "[--keep-gcode intermedio.gcode]"));
        return cmdSlice(args.at(1), args.at(2),
                        parser.value(profileOption),
                        parser.value(curaOption),
                        parser.value(definitionsOption),
                        parser.values(setOption),
                        parser.isSet(nameOption) ? parser.value(nameOption) : QString(),
                        parser.value(keepGcodeOption),
                        key);
    }

    if (command == QStringLiteral("wrap")) {
        if (args.size() < 3)
            return fail(QStringLiteral("uso: wrap <entrada.gcode> <salida.3w> [--name nombre.gcode]"));
        const QString name = parser.isSet(nameOption)
            ? parser.value(nameOption)
            : QFileInfo(args.at(1)).fileName();
        return cmdWrap(args.at(1), args.at(2), name, key);
    }

    if (command == QStringLiteral("encode")) {
        if (args.size() < 3)
            return fail(QStringLiteral("uso: encode <entrada.gcode> <salida.3w>"));
        return cmdEncode(args.at(1), args.at(2), key);
    }

    if (command == QStringLiteral("decode")) {
        if (args.size() < 3)
            return fail(QStringLiteral("uso: decode <entrada.3w> <salida.gcode>"));
        return cmdDecode(args.at(1), args.at(2), key);
    }

    if (command == QStringLiteral("inspect")) {
        if (args.size() < 2)
            return fail(QStringLiteral("uso: inspect <entrada.3w>"));
        return cmdInspect(args.at(1), key);
    }

    return fail(QStringLiteral("comando desconocido: %1").arg(command));
}
