#include "xdsimageplugin.h"

#include <xds/xds.h>

#include <QImage>
#include <QLoggingCategory>
#include <QSize>
#include <QVariant>

#include <cstring>
#include <exception>
#include <span>
#include <vector>

Q_LOGGING_CATEGORY(lcXds, "xds.imageformat", QtWarningMsg)

namespace {

const QByteArray kFormat = QByteArrayLiteral("xds");

std::span<const std::uint8_t> bytesOf(const QByteArray& data)
{
    return {reinterpret_cast<const std::uint8_t*>(data.constData()), static_cast<std::size_t>(data.size())};
}

} // namespace

bool XdsHandler::canRead(QIODevice* device)
{
    if (device == nullptr || !device->isReadable()) {
        return false;
    }
    return xds::looksLikeXds(bytesOf(device->peek(static_cast<qint64>(xds::kHeaderSize))));
}

bool XdsHandler::canRead() const
{
    if (!canRead(device())) {
        return false;
    }
    // canRead() is const in the base class but must report the format.
    const_cast<XdsHandler*>(this)->setFormat(kFormat);
    return true;
}

bool XdsHandler::read(QImage* image)
{
    if (!canRead(device())) {
        return false;
    }
    const QByteArray data = device()->readAll();
    try {
        const xds::Texture texture = xds::Texture::parse(bytesOf(data));
        const xds::Image decoded = texture.decodeLevel(0);
        const QSize size(static_cast<int>(decoded.width), static_cast<int>(decoded.height));
        QImage result;
        if (!QImageIOHandler::allocateImage(size, QImage::Format_RGBA8888, &result)) {
            qCWarning(lcXds) << "cannot allocate a" << size << "image";
            return false;
        }
        const std::size_t rowBytes = static_cast<std::size_t>(decoded.width) * 4;
        for (int y = 0; y < size.height(); ++y) {
            std::memcpy(result.scanLine(y), decoded.rgba.data() + rowBytes * static_cast<std::size_t>(y), rowBytes);
        }
        *image = result;
        return true;
    } catch (const std::exception& e) {
        qCWarning(lcXds) << "cannot read texture:" << e.what();
        return false;
    }
}

bool XdsHandler::supportsOption(ImageOption option) const
{
    return option == Size || option == ImageFormat;
}

QVariant XdsHandler::option(ImageOption option) const
{
    if (option == ImageFormat) {
        return QImage::Format_RGBA8888;
    }
    if (option != Size || !canRead(device())) {
        return {};
    }
    const xds::Header header = xds::readHeader(bytesOf(device()->peek(static_cast<qint64>(xds::kHeaderSize))));
    const xds::TextureInfo info = xds::decodeFetchConstant(header.fetchConstant);
    return QSize(static_cast<int>(info.width), static_cast<int>(info.height));
}

QImageIOPlugin::Capabilities XdsPlugin::capabilities(QIODevice* device, const QByteArray& format) const
{
    if (format == kFormat) {
        return CanRead;
    }
    if (!format.isEmpty()) {
        return {};
    }
    return XdsHandler::canRead(device) ? Capabilities(CanRead) : Capabilities();
}

QImageIOHandler* XdsPlugin::create(QIODevice* device, const QByteArray& format) const
{
    auto* handler = new XdsHandler;
    handler->setDevice(device);
    handler->setFormat(format.isEmpty() ? kFormat : format);
    return handler;
}
