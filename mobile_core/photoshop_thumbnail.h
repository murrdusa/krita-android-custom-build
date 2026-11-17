// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QBuffer>
#include <QDataStream>
#include <QImage>
#include <QImageReader>
#include <QIODevice>
#include <QtEndian>
#include <atomic>

class MobilePhotoshopThumbnail final
{
public:
    static QImage read(QIODevice &input,const std::atomic_bool *cancelled=nullptr)
    {
        const auto stopped=[cancelled]() {return cancelled && cancelled->load();};
        if(stopped())return {};
        const QByteArray header=input.read(26);
        if(header.size()!=26 || header.left(4)!="8BPS")return {};
        const quint16 version=qFromBigEndian<quint16>(
            reinterpret_cast<const uchar*>(header.constData()+4));
        if(version!=1 && version!=2)return {};
        // Only inspect the header and resource section. Bound both reads
        // and skipped bytes even for an Android pipe or malformed PSD.
        constexpr quint32 maxSection=8*1024*1024;
        QDataStream source(&input);
        quint32 colorBytes=0,resourceBytes=0;
        source>>colorBytes;
        if(source.status()!=QDataStream::Ok || colorBytes>maxSection)return {};
        char skip[4096];
        while(colorBytes>0) {
            if(stopped())return {};
            const qint64 count=input.read(skip,qMin<quint32>(colorBytes,sizeof(skip)));
            if(count<=0)return {};
            colorBytes-=quint32(count);
        }
        source>>resourceBytes;
        if(source.status()!=QDataStream::Ok || resourceBytes>maxSection || stopped())return {};
        QByteArray resources;
        resources.reserve(int(resourceBytes));
        while(resources.size()<int(resourceBytes)) {
            if(stopped())return {};
            const QByteArray chunk=input.read(qMin(32768,int(resourceBytes)-resources.size()));
            if(chunk.isEmpty())return {};
            resources.append(chunk);
        }
        QBuffer buffer(&resources);
        buffer.open(QIODevice::ReadOnly);
        QDataStream entries(&buffer);
        for(int count=0;count<4096 && !buffer.atEnd() && !stopped();++count) {
            char signature[4];
            quint16 id=0;
            quint8 nameBytes=0;
            if(entries.readRawData(signature,4)!=4 || QByteArray(signature,4)!="8BIM")return {};
            entries>>id>>nameBytes;
            const int paddedName=(int(nameBytes)+2)&~1;
            if(!buffer.seek(buffer.pos()+paddedName-1))return {};
            quint32 bytes=0;
            entries>>bytes;
            if(entries.status()!=QDataStream::Ok || bytes>quint64(buffer.bytesAvailable()))return {};
            const qint64 start=buffer.pos();
            if((id==1036 || id==1033) && bytes>=28 && bytes<=2*1024*1024) {
                quint32 format=0,width=0,height=0,rowBytes=0,total=0,compressed=0;
                quint16 depth=0,planes=0;
                entries>>format>>width>>height>>rowBytes>>total>>compressed>>depth>>planes;
                if(format==1 && width>0 && height>0 && width<=2048 && height<=2048 &&
                   quint64(width)*height<=1024*1024 && depth==24 && planes==1 &&
                   compressed>0 && compressed<=bytes-28) {
                    QByteArray jpeg=buffer.read(compressed);
                    QBuffer imageData(&jpeg);
                    imageData.open(QIODevice::ReadOnly);
                    QImageReader reader(&imageData,"jpeg");
                    const QSize size=reader.size();
                    if(size.isEmpty() || size.width()>2048 || size.height()>2048 ||
                       qint64(size.width())*size.height()>1024*1024)return {};
                    if(size.width()>124 || size.height()>124)
                        reader.setScaledSize(size.scaled(124,124,Qt::KeepAspectRatio));
                    if(stopped())return {};
                    QImage image=reader.read();
                    if(stopped())return {};
                    if(image.width()>124 || image.height()>124)
                        image=image.scaled(124,124,Qt::KeepAspectRatio,Qt::SmoothTransformation);
                    // Photoshop 4's resource used BGR instead of RGB.
                    if(id==1033)image=image.rgbSwapped();
                    return image;
                }
            }
            const qint64 next=start+bytes+(bytes&1);
            if(next>buffer.size() || !buffer.seek(next))return {};
        }
        return {};
    }
};
