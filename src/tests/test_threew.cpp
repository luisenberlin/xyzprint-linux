// SPDX-License-Identifier: MIT
//
// test_threew.cpp — regresión del códec .3w contra vectores validados.
//
// Los SHA-256 esperados corresponden a ficheros generados por XYZ3W_64.dll:
//   flat_null.3w  B45D6FB477097B309F3D9730E0AD289D03C66E48763812A970E5100F7FB5BFE5
//   test_null.3w  9FB6CC4102858DFDE35A92C4C1F8C005ABECDFD9297751E2FC28AAB46CA3D930

#include "../threew.h"

#include <QCryptographicHash>
#include <QtTest>

namespace {

QByteArray flatGcode()
{
    // 240 B exactos: prefijo de 50 B + 190 'X'.
    return QByteArray("; filename = a.gcode; machine = daVinciAW10; G21 ;")
         + QByteArray(190, 'X');
}

QByteArray testGcode()
{
    return QByteArray(
        "; filename = test.gcode\n"
        "; machine = daVinciAW10\n"
        "; layer height = 0.20\n"
        "; nozzle = 0.4\n"
        "; material = PLA\n"
        "G21 ;\n"
        "G90\n"
        "M82\n"
        "G28\n"
        "G1 Z0.3 F3000\n"
        "G1 X10 Y10 F6000\n"
        "G1 X100 Y10 E5 F1800\n"
        "G1 X100 Y100 E10\n"
        "G1 X10 Y100 E15\n"
        "G1 X10 Y10 E20\n"
        "M104 S0\n"
        "M84");
}

QByteArray sha256(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

} // namespace

class TestThreeW : public QObject
{
    Q_OBJECT

private slots:
    void encodeFlatMatchesDll();
    void encodeTestMatchesDll();
    void roundTrip();
    void crlfNormalization();
};

void TestThreeW::encodeFlatMatchesDll()
{
    QCOMPARE(flatGcode().size(), qsizetype(240));
    const QByteArray file = threew::encode(flatGcode());
    QCOMPARE(file.size(), qsizetype(8432));
    QCOMPARE(sha256(file),
             QByteArray("b45d6fb477097b309f3d9730e0ad289d03c66e48763812a970e5100f7fb5bfe5"));
}

void TestThreeW::encodeTestMatchesDll()
{
    QCOMPARE(testGcode().size(), qsizetype(231));
    const QByteArray file = threew::encode(testGcode());
    QCOMPARE(file.size(), qsizetype(8432));
    QCOMPARE(sha256(file),
             QByteArray("9fb6cc4102858dfde35a92c4c1f8c005abecdfd9297751e2fc28aab46ca3d930"));
}

void TestThreeW::roundTrip()
{
    for (const QByteArray &gcode : {flatGcode(), testGcode()}) {
        const QByteArray file = threew::encode(gcode);
        QVERIFY(!file.isEmpty());

        threew::Decoded decoded;
        QString error;
        QVERIFY2(threew::decode(file, threew::defaultKey(), &decoded, &error),
                 qPrintable(error));
        QCOMPARE(decoded.gcode, gcode);
        QVERIFY(decoded.crcOk);
        QCOMPARE(decoded.textLength, quint32(gcode.indexOf("G21 ;")));
    }
}

void TestThreeW::crlfNormalization()
{
    QCOMPARE(threew::toCrlf(QByteArray("a\nb\r\nc\rd")),
             QByteArray("a\r\nb\r\nc\r\nd"));
    QCOMPARE(threew::toCrlf(QByteArray("a\r\nb\r\n")),
             QByteArray("a\r\nb\r\n"));
    QCOMPARE(threew::toCrlf(QByteArray()), QByteArray());
}

QTEST_APPLESS_MAIN(TestThreeW)
#include "test_threew.moc"
