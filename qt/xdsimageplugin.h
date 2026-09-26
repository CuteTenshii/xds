#pragma once

#include <QImageIOHandler>
#include <QImageIOPlugin>

/// Reads Xbox 360 .xds textures for QImageReader, and so for Gwenview and
/// other Qt applications. The top mip level is decoded with its swizzle
/// applied, the way the game samples it.
class XdsHandler : public QImageIOHandler {
public:
    bool canRead() const override;
    bool read(QImage* image) override;
    bool supportsOption(ImageOption option) const override;
    QVariant option(ImageOption option) const override;

    /// Checks the header without consuming anything from `device`.
    static bool canRead(QIODevice* device);
};

class XdsPlugin : public QImageIOPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QImageIOHandlerFactoryInterface_iid FILE "xds.json")

public:
    Capabilities capabilities(QIODevice* device, const QByteArray& format) const override;
    QImageIOHandler* create(QIODevice* device, const QByteArray& format = QByteArray()) const override;
};
