// SPDX-License-Identifier: MIT
//
// slicer.h — invocación de CuraEngine con un perfil reconstruido para la
// da Vinci 1.0 Pro.
//
// No contiene código de XYZprinting. El perfil y la secuencia de arranque se
// dedujeron de un .3w real generado por XYZprint para daVinciAW10; ver
// docs/perfiles-y-maquina.md.

#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>

namespace xyz {

struct SliceProfile {
    QString name;
    QString description;
    QMap<QString, QString> global;
    QMap<QString, QString> extruder;
};

struct SliceRequest {
    QString modelPath;
    QString profilePath;
    QString curaEngine;
    QString definitionsDir;
    QStringList overrides;
    QString name;
};

struct SliceResult {
    QByteArray gcode;
    QString profileName;
    int estimatedSeconds = -1;
    int layers = -1;
    double filamentMeters = -1.0;
};

// Carga y valida un perfil JSON con objetos "global" y "extruder".
bool loadProfile(const QString &path, SliceProfile *profile, QString *error);

// Ejecuta CuraEngine y devuelve el G-code con la cabecera de metadatos de
// daVinciAW10. No escribe el .3w; eso lo hace el llamador con threew::encode.
bool slice(const SliceRequest &request, SliceResult *result, QString *error);

// Rutas por defecto; devuelven una cadena vacía si no encuentran nada.
QString defaultCuraEngine();
QString defaultDefinitionsDir();
QString defaultProfilePath();

} // namespace xyz
