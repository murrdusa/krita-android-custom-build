// SPDX-License-Identifier: GPL-2.0-or-later
#include <QCoreApplication>
#include <QDebug>
#include "mobile_core/photoshop_thumbnail.h"

static void require(bool condition,const char *message)
{
    if(!condition)qFatal("Photoshop thumbnail: %s",message);
}

class SequentialInput final : public QIODevice
{
public:
    explicit SequentialInput(const QByteArray &bytes) : m_bytes(bytes) {open(ReadOnly);}
    bool isSequential() const override {return true;}
protected:
    qint64 readData(char *data,qint64 maximum) override
    {
        const auto count=qMin(maximum,qint64(m_bytes.size()-m_position));
        memcpy(data,m_bytes.constData()+m_position,size_t(count));
        m_position+=int(count);
        return count;
    }
    qint64 writeData(const char *,qint64) override {return -1;}
private:
    QByteArray m_bytes;
    int m_position=0;
};

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    QImage original(320,240,QImage::Format_RGB32);
    original.fill(QColor(230,30,20));
    QByteArray jpeg;
    QBuffer image(&jpeg);
    image.open(QIODevice::WriteOnly);
    require(original.save(&image,"JPEG"),"JPEG fixture failed");
    QByteArray resource;
    QDataStream entry(&resource,QIODevice::WriteOnly);
    entry.writeRawData("8BIM",4);
    entry<<quint16(1036)<<quint8(0)<<quint8(0)<<quint32(28+jpeg.size());
    entry<<quint32(1)<<quint32(320)<<quint32(240)<<quint32(960)<<quint32(230400)
         <<quint32(jpeg.size())<<quint16(24)<<quint16(1);
    entry.writeRawData(jpeg.constData(),jpeg.size());
    if(jpeg.size()&1)entry<<quint8(0);
    for(const auto version:{quint16(1),quint16(2)}) {
        QByteArray psd;
        QDataStream file(&psd,QIODevice::WriteOnly);
        file.writeRawData("8BPS",4);
        file<<version;
        file.writeRawData("\0\0\0\0\0\0",6);
        file<<quint16(3)<<quint32(240)<<quint32(320)<<quint16(8)<<quint16(3);
        file<<quint32(0)<<quint32(resource.size());
        file.writeRawData(resource.constData(),resource.size());
        psd.append(QByteArray(1024,'x')); // Layer data must never be parsed.
        SequentialInput stream(psd);
        const QImage thumbnail=MobilePhotoshopThumbnail::read(stream);
        require(thumbnail.size()==QSize(124,93) && thumbnail.pixelColor(0,0).red()>200,
                "PSD/PSB embedded JPEG was not decoded on a sequential stream");
        QBuffer truncated(&psd);
        truncated.open(QIODevice::ReadOnly);
        std::atomic_bool cancelled{true};
        require(MobilePhotoshopThumbnail::read(truncated,&cancelled).isNull() &&
                truncated.pos()==0,"cancelled preview consumed the file");
        psd.truncate(36);
        QBuffer broken(&psd);
        broken.open(QIODevice::ReadOnly);
        require(MobilePhotoshopThumbnail::read(broken).isNull(),"truncated resources accepted");
    }
}
