// SPDX-License-Identifier: MIT
//
// slicer.cpp — ejecuta CuraEngine con el perfil de la da Vinci 1.0 Pro.
//
// La secuencia de arranque (M108, M191, M140/M190, M104/M109) y los valores
// de referencia se obtuvieron de un .3w real generado por XYZprint 2.1.1 para
// daVinciAW10; ver docs/perfiles-y-maquina.md. No es código de XYZprinting.

#include "slicer.h"
#include "threew.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cmath>

namespace xyz {
namespace {

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

QString jsonToString(const QJsonValue &value)
{
    if (value.isBool())
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (std::isfinite(number) && std::floor(number) == number && std::abs(number) < 1e15)
            return QString::number(qint64(number));
        return QString::number(number, 'g', 15);
    }
    if (value.isString())
        return value.toString();
    return {};
}

bool readSettingsObject(const QJsonObject &root,
                        const QString &key,
                        QMap<QString, QString> *settings,
                        QString *error)
{
    const QJsonValue value = root.value(key);
    if (value.isUndefined())
        return true;
    if (!value.isObject()) {
        setError(error, QStringLiteral("\"%1\" debe ser un objeto JSON").arg(key));
        return false;
    }

    const QJsonObject object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (!it.value().isString() && !it.value().isDouble() && !it.value().isBool()) {
            setError(error, QStringLiteral("\"%1.%2\" debe ser texto, número o booleano")
                                .arg(key, it.key()));
            return false;
        }
        settings->insert(it.key(), jsonToString(it.value()));
    }
    return true;
}

bool resolvePlaceholders(const QString &text,
                         const QMap<QString, QString> &values,
                         QString *resolved,
                         QString *error)
{
    QString result = text;
    const QRegularExpression expression(QStringLiteral("\\{([A-Za-z0-9_]+)\\}"));

    for (int pass = 0; pass < 5; ++pass) {
        QString output;
        int last = 0;
        bool changed = false;

        auto it = expression.globalMatch(result);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            output += result.mid(last, match.capturedStart() - last);
            const QString key = match.captured(1);
            if (!values.contains(key)) {
                setError(error, QStringLiteral("placeholder {%1} sin valor en el perfil").arg(key));
                return false;
            }
            output += values.value(key);
            last = match.capturedEnd();
            changed = true;
        }
        output += result.mid(last);
        result = output;

        if (!changed)
            break;
    }

    if (result.contains(QLatin1Char('{'))) {
        setError(error, QStringLiteral("no se pudieron resolver todos los placeholders del G-code"));
        return false;
    }

    *resolved = result;
    return true;
}

void applyOverride(const QString &rawKey,
                   const QString &value,
                   QMap<QString, QString> *global,
                   QMap<QString, QString> *extruder)
{
    QString key = rawKey.trimmed();
    enum class Target { Smart, Global, Extruder } target = Target::Smart;

    if (key.startsWith(QStringLiteral("e:"))) {
        key = key.mid(2);
        target = Target::Extruder;
    } else if (key.startsWith(QStringLiteral("extruder."))) {
        key = key.mid(9);
        target = Target::Extruder;
    } else if (key.startsWith(QStringLiteral("g:"))) {
        key = key.mid(2);
        target = Target::Global;
    } else if (key.startsWith(QStringLiteral("global."))) {
        key = key.mid(7);
        target = Target::Global;
    }

    if (key.isEmpty())
        return;

    if (target == Target::Extruder) {
        extruder->insert(key, value);
        return;
    }
    if (target == Target::Global) {
        global->insert(key, value);
        return;
    }

    const bool inGlobal = global->contains(key);
    const bool inExtruder = extruder->contains(key);
    if (inGlobal)
        global->insert(key, value);
    if (inExtruder)
        extruder->insert(key, value);
    if (!inGlobal && !inExtruder)
        global->insert(key, value);
}

bool applyOverrides(const QStringList &overrides,
                    QMap<QString, QString> *global,
                    QMap<QString, QString> *extruder,
                    QString *error)
{
    for (const QString &override : overrides) {
        const int equals = override.indexOf(QLatin1Char('='));
        if (equals <= 0) {
            setError(error, QStringLiteral("--set espera clave=valor: %1").arg(override));
            return false;
        }
        applyOverride(override.left(equals), override.mid(equals + 1), global, extruder);
    }
    return true;
}

QString firstSetting(const QMap<QString, QString> &primary,
                     const QMap<QString, QString> &secondary,
                     const QString &key,
                     const QString &fallback = {})
{
    if (primary.contains(key))
        return primary.value(key);
    if (secondary.contains(key))
        return secondary.value(key);
    return fallback;
}

// CuraEngine se invoca por CLI sin la GUI de Cura, y en ese modo deja
// ";TIME:6666" y ";Filament used: 0m" como marcadores: no calcula ni tiempo ni
// filamento. Se estiman aquí a partir de los movimientos del propio G-code.
// No sustituye al estimador real de Cura (ignora aceleración), pero da cifras
// del orden correcto en el LCD y en la cabecera del .3w.
void analyzeGcode(const QByteArray &gcode, int *seconds, double *filamentMeters)
{
    double x = 0, y = 0, z = 0, e = 0, f = 0;
    double totalSeconds = 0.0;
    double totalExtrusion = 0.0;

    int position = 0;
    while (position < gcode.size()) {
        int end = gcode.indexOf('\n', position);
        if (end < 0)
            end = gcode.size();
        const QByteArray line = gcode.mid(position, end - position).trimmed();
        position = end + 1;

        if (line.startsWith("G1 ") || line.startsWith("G0 ")) {
            double nx = x, ny = y, nz = z, ne = e, nf = f;
            const QList<QByteArray> words = line.mid(3).split(' ');
            for (const QByteArray &word : words) {
                if (word.size() < 2)
                    continue;
                bool ok = false;
                const double value = word.mid(1).toDouble(&ok);
                if (!ok)
                    continue;
                switch (word.at(0)) {
                case 'X': nx = value; break;
                case 'Y': ny = value; break;
                case 'Z': nz = value; break;
                case 'E': ne = value; break;
                case 'F': nf = value; break;
                default: break;
                }
            }
            const double distance = std::sqrt((nx - x) * (nx - x)
                                              + (ny - y) * (ny - y)
                                              + (nz - z) * (nz - z));
            if (ne > e)
                totalExtrusion += ne - e;
            if (nf > 0 && distance > 0.0)
                totalSeconds += distance / (nf / 60.0);
            // Retracción/cebado sin desplazamiento: coste despreciable pero real.
            else if (std::abs(ne - e) > 0.0)
                totalSeconds += 0.05;
            x = nx; y = ny; z = nz; e = ne; f = nf;
        } else if (line.startsWith("G4 ")) {
            bool ok = false;
            const double ms = line.mid(3).trimmed().toDouble(&ok);
            if (ok)
                totalSeconds += ms / 1000.0;
        } else if (line.startsWith("G92 ")) {
            for (const QByteArray &word : line.mid(4).split(' ')) {
                bool ok = false;
                const double value = word.mid(1).toDouble(&ok);
                if (!ok || word.isEmpty())
                    continue;
                if (word.at(0) == 'E')
                    e = value;
            }
        }
    }

    // Calentamiento de cama y boquilla antes de la primera capa.
    if (seconds)
        *seconds = int(totalSeconds) + 120;
    if (filamentMeters)
        *filamentMeters = totalExtrusion / 1000.0;
}

int countOccurrences(const QByteArray &data, const QByteArray &needle)
{
    int count = 0;
    int position = 0;
    while ((position = data.indexOf(needle, position)) >= 0) {
        ++count;
        position += needle.size();
    }
    return count;
}

// La impresora y el .3w de referencia usan G1 incluso para desplazamientos.
// Convertir G0 -> G1 evita depender de que el firmware acepte G0.
QByteArray normalizeG0(QByteArray gcode)
{
    if (gcode.startsWith("G0 "))
        gcode.replace(0, 2, "G1");

    int position = 0;
    while ((position = gcode.indexOf("\nG0 ", position)) >= 0) {
        gcode.replace(position + 1, 2, "G1");
        position += 3;
    }
    return gcode;
}

QString metadataHeader(const SliceResult &result,
                       const SliceProfile &profile,
                       const QString &name)
{
    const QString nozzleValue = firstSetting(profile.extruder, profile.global,
                                             QStringLiteral("machine_nozzle_size"),
                                             QStringLiteral("0.4"));
    bool nozzleOk = false;
    const double nozzle = nozzleValue.toDouble(&nozzleOk);
    const QString nozzleText = nozzleOk ? QString::number(nozzle, 'f', 2) : nozzleValue;

    const QString temperature = firstSetting(profile.extruder, profile.global,
                                             QStringLiteral("material_print_temperature_layer_0"),
                                             firstSetting(profile.extruder, profile.global,
                                                          QStringLiteral("material_print_temperature"),
                                                          QStringLiteral("210")));
    const QString bedTemperature = firstSetting(profile.global, profile.extruder,
                                                QStringLiteral("material_bed_temperature_layer_0"),
                                                firstSetting(profile.global, profile.extruder,
                                                             QStringLiteral("material_bed_temperature"),
                                                             QStringLiteral("45")));

    QString header;
    header += QStringLiteral("; filename = %1\n").arg(name);
    if (result.estimatedSeconds >= 0)
        header += QStringLiteral("; print_time = %1\n").arg(result.estimatedSeconds);
    header += QStringLiteral("; machine = daVinciAW10\n");
    if (result.layers > 0)
        header += QStringLiteral("; total_layers = %1\n").arg(result.layers);
    header += QStringLiteral("; nozzle_diameter = %1\n").arg(nozzleText);
    header += QStringLiteral("; temperature = %1\n").arg(temperature);
    header += QStringLiteral("; bed_temperature = %1\n").arg(bedTemperature);
    header += QStringLiteral("; layer_height = %1\n")
                  .arg(firstSetting(profile.global, profile.extruder,
                                    QStringLiteral("layer_height"), QStringLiteral("0.2")));
    header += QStringLiteral("; first_layer_height = %1\n")
                  .arg(firstSetting(profile.global, profile.extruder,
                                    QStringLiteral("layer_height_0"), QStringLiteral("0.3")));
    header += QStringLiteral("; fill_density = %1\n")
                  .arg(firstSetting(profile.global, profile.extruder,
                                    QStringLiteral("infill_sparse_density"), QStringLiteral("10")));
    header += QStringLiteral("; shells = %1\n")
                  .arg(firstSetting(profile.global, profile.extruder,
                                    QStringLiteral("wall_line_count"), QStringLiteral("2")));
    header += QStringLiteral("; top_solid_layers = %1\n")
                  .arg(firstSetting(profile.global, profile.extruder,
                                    QStringLiteral("top_layers"), QStringLiteral("3")));
    header += QStringLiteral("; bottom_solid_layers = %1\n")
                  .arg(firstSetting(profile.global, profile.extruder,
                                    QStringLiteral("bottom_layers"), QStringLiteral("3")));
    header += QStringLiteral("; retract_length = %1\n")
                  .arg(firstSetting(profile.extruder, profile.global,
                                    QStringLiteral("retraction_amount"), QStringLiteral("4")));
    header += QStringLiteral("; retract_speed = %1\n")
                  .arg(firstSetting(profile.extruder, profile.global,
                                    QStringLiteral("retraction_speed"), QStringLiteral("30")));
    header += QStringLiteral("; travel_speed = %1\n")
                  .arg(firstSetting(profile.global, profile.extruder,
                                    QStringLiteral("speed_travel"), QStringLiteral("100")));
    header += QStringLiteral("; first_layer_speed = %1\n")
                  .arg(firstSetting(profile.global, profile.extruder,
                                    QStringLiteral("speed_layer_0"), QStringLiteral("20")));
    header += QStringLiteral("; speed = %1\n")
                  .arg(firstSetting(profile.global, profile.extruder,
                                    QStringLiteral("speed_print"), QStringLiteral("30")));
    header += QStringLiteral("; materialid = 0\n");
    if (result.filamentMeters >= 0.0) {
        header += QStringLiteral("; total_filament = %1\n")
                      .arg(result.filamentMeters * 1000.0, 0, 'f', 2);
    }
    header += QStringLiteral("; generated_by = xyzprint-cli + CuraEngine\n");
    return header;
}

} // namespace

QString defaultCuraEngine()
{
    const QString fromEnvironment = qEnvironmentVariable("CURA_ENGINE");
    if (!fromEnvironment.isEmpty())
        return fromEnvironment;

    const QString inPath = QStandardPaths::findExecutable(QStringLiteral("CuraEngine"));
    if (!inPath.isEmpty())
        return inPath;

    const QString applicationDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QStringLiteral("/usr/bin/CuraEngine"),
        QStringLiteral("/usr/local/bin/CuraEngine"),
        QStringLiteral("/usr/lib/cura/CuraEngine"),
        QStringLiteral("/usr/lib/ultimaker-cura/CuraEngine"),
        QStringLiteral("/opt/cura/CuraEngine"),
        QStringLiteral("/app/bin/CuraEngine"),
        applicationDir + QStringLiteral("/CuraEngine"),
        applicationDir + QStringLiteral("/../bin/CuraEngine"),
        applicationDir + QStringLiteral("/../lib/CuraEngine")
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return QStringLiteral("CuraEngine");
}

QString defaultDefinitionsDir()
{
    const QString fromEnvironment = qEnvironmentVariable("CURA_DEFINITIONS");
    if (!fromEnvironment.isEmpty())
        return fromEnvironment;

    const QString applicationDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QStringLiteral("/usr/share/cura/resources/definitions"),
        QStringLiteral("/usr/share/ultimaker-cura/resources/definitions"),
        QStringLiteral("/usr/share/cura-engine/definitions"),
        QStringLiteral("/usr/lib/cura/resources/definitions"),
        QStringLiteral("/usr/lib/ultimaker-cura/resources/definitions"),
        QStringLiteral("/opt/cura/resources/definitions"),
        QStringLiteral("/app/share/cura/resources/definitions"),
        applicationDir + QStringLiteral("/../share/cura/resources/definitions"),
        applicationDir + QStringLiteral("/../share/xyzprint/definitions"),
        applicationDir + QStringLiteral("/definitions")
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate + QStringLiteral("/fdmprinter.def.json")))
            return candidate;
    }
    return {};
}

QString defaultProfilePath()
{
    const QString fromEnvironment = qEnvironmentVariable("XYZPRINT_PROFILE");
    if (!fromEnvironment.isEmpty())
        return fromEnvironment;

    const QString applicationDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        applicationDir + QStringLiteral("/../share/xyzprint/profiles/davinci-1.0-pro-pla.json"),
        applicationDir + QStringLiteral("/profiles/davinci-1.0-pro-pla.json"),
        applicationDir + QStringLiteral("/../profiles/davinci-1.0-pro-pla.json"),
        QStringLiteral("profiles/davinci-1.0-pro-pla.json"),
        QStringLiteral("../profiles/davinci-1.0-pro-pla.json")
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate))
            return QFileInfo(candidate).absoluteFilePath();
    }
    return {};
}

bool loadProfile(const QString &path, SliceProfile *profile, QString *error)
{
    if (!profile) {
        setError(error, QStringLiteral("loadProfile: perfil nulo"));
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("no se pudo leer el perfil %1: %2")
                            .arg(path, file.errorString()));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QStringLiteral("perfil inválido %1: %2")
                            .arg(path, parseError.errorString()));
        return false;
    }

    const QJsonObject root = document.object();
    profile->name = root.value(QStringLiteral("name"))
                        .toString(QFileInfo(path).completeBaseName());
    profile->description = root.value(QStringLiteral("description")).toString();
    profile->global.clear();
    profile->extruder.clear();

    if (!readSettingsObject(root, QStringLiteral("global"), &profile->global, error))
        return false;
    if (!readSettingsObject(root, QStringLiteral("extruder"), &profile->extruder, error))
        return false;

    if (profile->global.isEmpty()) {
        setError(error, QStringLiteral("el perfil %1 no tiene ajustes globales").arg(path));
        return false;
    }
    return true;
}

bool slice(const SliceRequest &request, SliceResult *result, QString *error)
{
    if (!result) {
        setError(error, QStringLiteral("slice: resultado nulo"));
        return false;
    }

    const QString modelPath = QFileInfo(request.modelPath).absoluteFilePath();
    if (!QFileInfo::exists(modelPath)) {
        setError(error, QStringLiteral("no existe el modelo: %1").arg(request.modelPath));
        return false;
    }

    const QString profilePath = request.profilePath.isEmpty()
        ? defaultProfilePath()
        : QFileInfo(request.profilePath).absoluteFilePath();
    if (profilePath.isEmpty()) {
        setError(error, QStringLiteral(
            "no se encontró el perfil de CuraEngine; pasa --profile <ruta> o define XYZPRINT_PROFILE"));
        return false;
    }

    SliceProfile profile;
    if (!loadProfile(profilePath, &profile, error))
        return false;

    if (!applyOverrides(request.overrides, &profile.global, &profile.extruder, error))
        return false;

    QMap<QString, QString> values = profile.global;
    for (auto it = profile.extruder.cbegin(); it != profile.extruder.cend(); ++it)
        values.insert(it.key(), it.value());

    const QStringList gcodeKeys = {
        QStringLiteral("machine_start_gcode"),
        QStringLiteral("machine_end_gcode")
    };
    for (const QString &key : gcodeKeys) {
        if (!profile.global.contains(key))
            continue;
        QString resolved;
        if (!resolvePlaceholders(profile.global.value(key), values, &resolved, error))
            return false;
        profile.global.insert(key, resolved);
        values.insert(key, resolved);
    }

    const QString startGcode = profile.global.value(QStringLiteral("machine_start_gcode"));
    if (!startGcode.contains(QStringLiteral("G21 ;"))) {
        setError(error, QStringLiteral(
            "machine_start_gcode no contiene \"G21 ;\"; el .3w lo necesita como marcador"));
        return false;
    }

    QString curaEngine = request.curaEngine.isEmpty()
        ? defaultCuraEngine()
        : request.curaEngine;
    if (!curaEngine.contains(QLatin1Char('/')) && !curaEngine.contains(QLatin1Char('\\'))) {
        const QString inPath = QStandardPaths::findExecutable(curaEngine);
        if (inPath.isEmpty()) {
            setError(error, QStringLiteral(
                "no se encontró CuraEngine en PATH; pasa --cura <ruta> o define CURA_ENGINE"));
            return false;
        }
        curaEngine = inPath;
    } else {
        curaEngine = QFileInfo(curaEngine).absoluteFilePath();
        if (!QFileInfo::exists(curaEngine)) {
            setError(error, QStringLiteral("no existe CuraEngine: %1").arg(curaEngine));
            return false;
        }
    }

    const QString definitionsDir = request.definitionsDir.isEmpty()
        ? defaultDefinitionsDir()
        : QFileInfo(request.definitionsDir).absoluteFilePath();
    if (definitionsDir.isEmpty()) {
        setError(error, QStringLiteral(
            "no se encontró el directorio de definiciones de CuraEngine; "
            "pasa --definitions <ruta> (debe contener fdmprinter.def.json)"));
        return false;
    }

    const QString printerDefinition = QDir(definitionsDir)
                                          .filePath(QStringLiteral("fdmprinter.def.json"));
    const QString extruderDefinition = QDir(definitionsDir)
                                           .filePath(QStringLiteral("fdmextruder.def.json"));
    if (!QFileInfo::exists(printerDefinition) || !QFileInfo::exists(extruderDefinition)) {
        setError(error, QStringLiteral(
            "faltan fdmprinter.def.json o fdmextruder.def.json en %1").arg(definitionsDir));
        return false;
    }

    QTemporaryDir temporaryDir(QDir::tempPath() + QStringLiteral("/xyzprint-slice-XXXXXX"));
    if (!temporaryDir.isValid()) {
        setError(error, QStringLiteral("no se pudo crear un directorio temporal"));
        return false;
    }
    const QString gcodePath = temporaryDir.filePath(QStringLiteral("output.gcode"));

    const auto addSettings = [](QStringList *arguments,
                                const QMap<QString, QString> &settings) {
        for (auto it = settings.cbegin(); it != settings.cend(); ++it)
            *arguments << QStringLiteral("-s") << (it.key() + QLatin1Char('=') + it.value());
    };

    QStringList arguments;
    arguments << QStringLiteral("slice") << QStringLiteral("-v")
              << QStringLiteral("-j") << printerDefinition;
    addSettings(&arguments, profile.global);

    // Después de -e0 los ajustes se aplican al extrusor 0. Los globales ya se
    // pasaron antes; aquí solo van los de material/retracción del extrusor.
    arguments << QStringLiteral("-e0") << QStringLiteral("-j") << extruderDefinition;
    addSettings(&arguments, profile.extruder);

    arguments << QStringLiteral("-l") << modelPath
              << QStringLiteral("-o") << gcodePath;

    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    QString searchPath = definitionsDir;
    const QString existingSearchPath = environment.value(
        QStringLiteral("CURA_ENGINE_SEARCH_PATH"));
    if (!existingSearchPath.isEmpty())
        searchPath += QDir::listSeparator() + existingSearchPath;
    environment.insert(QStringLiteral("CURA_ENGINE_SEARCH_PATH"), searchPath);
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setWorkingDirectory(temporaryDir.path());
    process.start(curaEngine, arguments);

    if (!process.waitForStarted(15000)) {
        setError(error, QStringLiteral("no se pudo ejecutar %1: %2")
                            .arg(curaEngine, process.errorString()));
        return false;
    }
    if (!process.waitForFinished(-1)) {
        process.kill();
        process.waitForFinished(3000);
        setError(error, QStringLiteral("CuraEngine no terminó: %1").arg(process.errorString()));
        return false;
    }

    const QByteArray log = process.readAll();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        setError(error, QStringLiteral("CuraEngine falló (código %1):\n%2")
                            .arg(process.exitCode())
                            .arg(QString::fromLocal8Bit(log).trimmed()));
        return false;
    }

    QFile gcodeFile(gcodePath);
    if (!gcodeFile.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("no se pudo leer el G-code generado: %1")
                            .arg(gcodeFile.errorString()));
        return false;
    }
    QByteArray gcode = gcodeFile.readAll();
    if (gcode.isEmpty()) {
        setError(error, QStringLiteral("CuraEngine no generó G-code:\n%1")
                            .arg(QString::fromLocal8Bit(log).trimmed()));
        return false;
    }

    gcode = normalizeG0(gcode);

    result->profileName = profile.name;
    result->estimatedSeconds = -1;
    result->layers = -1;
    result->filamentMeters = -1.0;

    const QString head = QString::fromLatin1(gcode.left(16384));
    const QRegularExpression timeExpression(QStringLiteral(";TIME:(\\d+)"));
    const QRegularExpression filamentExpression(
        QStringLiteral(";Filament used:\\s*([0-9.]+)m"));
    const QRegularExpression layerCountExpression(
        QStringLiteral(";LAYER_COUNT:(\\d+)"));

    const QRegularExpressionMatch timeMatch = timeExpression.match(head);
    if (timeMatch.hasMatch()) {
        const int seconds = timeMatch.captured(1).toInt();
        // 6666 es el marcador que deja CuraEngine cuando no estima (CLI).
        if (seconds > 0 && seconds != 6666)
            result->estimatedSeconds = seconds;
    }

    const QRegularExpressionMatch filamentMatch = filamentExpression.match(head);
    if (filamentMatch.hasMatch())
        result->filamentMeters = filamentMatch.captured(1).toDouble();

    if (result->estimatedSeconds < 0 || result->filamentMeters <= 0.0) {
        int seconds = 0;
        double meters = 0.0;
        analyzeGcode(gcode, &seconds, &meters);
        if (result->estimatedSeconds < 0)
            result->estimatedSeconds = seconds;
        if (result->filamentMeters <= 0.0 && meters > 0.0)
            result->filamentMeters = meters;
    }

    result->layers = countOccurrences(gcode, QByteArrayLiteral(";LAYER:"));
    if (result->layers <= 0) {
        const QRegularExpressionMatch layerMatch = layerCountExpression.match(head);
        if (layerMatch.hasMatch())
            result->layers = layerMatch.captured(1).toInt();
    }

    const QString name = request.name.isEmpty()
        ? QFileInfo(modelPath).completeBaseName() + QStringLiteral(".gcode")
        : request.name;
    gcode.prepend(metadataHeader(*result, profile, name).toUtf8());
    // El .3w real de XYZprint usa CRLF en todo el G-code; el firmware lo acepta
    // así y la cabecera queda idéntica en estilo.
    result->gcode = threew::toCrlf(gcode);
    return true;
}

} // namespace xyz
