#include "synthetic.h"

#include <xds/xds.h>

#include <QBuffer>
#include <QCoreApplication>
#include <QImage>
#include <QImageReader>
#include <QTemporaryDir>
#include <QTest>

#include <cstring>

namespace {

QByteArray toQByteArray(const std::vector<std::uint8_t>& bytes)
{
    return QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
}

/// A 40x20 ARGB texture with mips whose texel (x, y) is (x, y, 7, 200).
std::vector<std::uint8_t> sampleTexture()
{
    synthetic::Spec spec;
    spec.format = 6;
    spec.width = 40;
    spec.height = 20;
    spec.maxLevel = 5;
    spec.swizzle = {xds::Swizzle::Z, xds::Swizzle::Y, xds::Swizzle::X, xds::Swizzle::W};
    return synthetic::build(spec, [](std::uint32_t, std::uint32_t x, std::uint32_t y) {
        return std::vector<std::uint8_t>{7, static_cast<std::uint8_t>(y), static_cast<std::uint8_t>(x), 200};
    });
}

} // namespace

class TestQtPlugin : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QCoreApplication::addLibraryPath(QStringLiteral(XDS_TEST_PLUGIN_DIR));
    }

    void pluginIsRegistered()
    {
        QVERIFY(QImageReader::supportedImageFormats().contains("xds"));
        QVERIFY(QImageReader::supportedMimeTypes().contains("image/x-xds"));
    }

    void readsByContentAlone()
    {
        const std::vector<std::uint8_t> bytes = sampleTexture();
        QByteArray data = toQByteArray(bytes);
        QBuffer buffer(&data);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        QVERIFY(reader.canRead());
        QCOMPARE(reader.format(), QByteArray("xds"));
        QCOMPARE(reader.size(), QSize(40, 20));

        const QImage image = reader.read();
        QVERIFY2(!image.isNull(), qPrintable(reader.errorString()));
        QCOMPARE(image.size(), QSize(40, 20));
        QCOMPARE(image.pixelColor(0, 0), QColor(0, 0, 7, 200));
        QCOMPARE(image.pixelColor(39, 19), QColor(39, 19, 7, 200));

        const xds::Image expected = xds::Texture::parse(bytes).decodeLevel(0);
        const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
        for (int y = 0; y < rgba.height(); ++y) {
            QVERIFY(std::memcmp(rgba.constScanLine(y), expected.rgba.data() + static_cast<std::size_t>(y) * 40 * 4,
                        40 * 4)
                == 0);
        }
    }

    void readsFromAFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("sample.xds"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(toQByteArray(sampleTexture()));
        file.close();

        const QImage image(path);
        QVERIFY(!image.isNull());
        QCOMPARE(image.size(), QSize(40, 20));
    }

    void rejectsOtherData()
    {
        QByteArray data(4096, '\x5a');
        QBuffer buffer(&data);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "xds");
        QVERIFY(reader.read().isNull());

        buffer.seek(0);
        QImageReader autodetect(&buffer);
        QVERIFY(autodetect.format() != QByteArray("xds"));
    }

    void rejectsTruncatedTexture()
    {
        QByteArray data = toQByteArray(sampleTexture());
        data.truncate(data.size() / 2);
        QBuffer buffer(&data);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "xds");
        QVERIFY(reader.read().isNull());
    }
};

QTEST_GUILESS_MAIN(TestQtPlugin)
#include "tst_qtplugin.moc"
