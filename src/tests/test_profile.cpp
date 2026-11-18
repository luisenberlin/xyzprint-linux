// SPDX-License-Identifier: MIT
//
// test_profile.cpp — valida el perfil de CuraEngine para la 1.0 Pro.
//
// No ejecuta CuraEngine (puede no estar instalado); comprueba que el JSON se
// carga, que la cama es 200x200x200 y que la secuencia de arranque contiene los
// marcadores que el .3w y el firmware necesitan.

#include "../slicer.h"

#include <QDir>
#include <QtTest>

class TestProfile : public QObject
{
    Q_OBJECT

private slots:
    void loadBundledProfile();
    void startGcodeHasRequiredMarkers();
};

namespace {

QString profilePath()
{
    return QDir(QString::fromUtf8(XYZPRINT_SOURCE_DIR))
        .filePath(QStringLiteral("profiles/davinci-1.0-pro-pla.json"));
}

} // namespace

void TestProfile::loadBundledProfile()
{
    xyz::SliceProfile profile;
    QString error;
    QVERIFY2(xyz::loadProfile(profilePath(), &profile, &error), qPrintable(error));

    QCOMPARE(profile.global.value(QStringLiteral("machine_width")), QStringLiteral("200"));
    QCOMPARE(profile.global.value(QStringLiteral("machine_depth")), QStringLiteral("200"));
    QCOMPARE(profile.global.value(QStringLiteral("machine_height")), QStringLiteral("200"));
    QCOMPARE(profile.global.value(QStringLiteral("machine_heated_bed")), QStringLiteral("true"));
    QCOMPARE(profile.global.value(QStringLiteral("machine_gcode_flavor")),
             QStringLiteral("RepRap (Marlin/Sprinter)"));
    QCOMPARE(profile.extruder.value(QStringLiteral("machine_nozzle_size")),
             QStringLiteral("0.4"));
    QCOMPARE(profile.extruder.value(QStringLiteral("material_diameter")),
             QStringLiteral("1.75"));
    QCOMPARE(profile.extruder.value(QStringLiteral("retraction_amount")).toDouble(), 4.0);
    QCOMPARE(profile.extruder.value(QStringLiteral("retraction_speed")).toDouble(), 30.0);
}

void TestProfile::startGcodeHasRequiredMarkers()
{
    xyz::SliceProfile profile;
    QString error;
    QVERIFY2(xyz::loadProfile(profilePath(), &profile, &error), qPrintable(error));

    const QString start = profile.global.value(QStringLiteral("machine_start_gcode"));
    QVERIFY(start.contains(QStringLiteral("G21 ;")));
    QVERIFY(start.contains(QStringLiteral("M108")));
    QVERIFY(start.contains(QStringLiteral("M191")));
    QVERIFY(start.contains(QStringLiteral("{material_bed_temperature_layer_0}")));
    QVERIFY(start.contains(QStringLiteral("{material_print_temperature_layer_0}")));

    QVERIFY(profile.global.contains(QStringLiteral("material_bed_temperature_layer_0")));
    QVERIFY(profile.extruder.contains(QStringLiteral("material_print_temperature_layer_0")));

    const QString end = profile.global.value(QStringLiteral("machine_end_gcode"));
    QVERIFY(end.contains(QStringLiteral("M84")));
    QVERIFY(end.contains(QStringLiteral("M104 S0")));
    QVERIFY(end.contains(QStringLiteral("M140 S0")));
    QVERIFY(end.contains(QStringLiteral("E-4.00000")));
    QVERIFY(!end.contains(QStringLiteral("G91")));
}

QTEST_APPLESS_MAIN(TestProfile)
#include "test_profile.moc"
