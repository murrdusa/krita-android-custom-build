// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QFileInfo>
#include <QRegularExpression>
#include <KoResource.h>
#include <KoResourcePaths.h>

namespace MobileFactoryBrushPreset
{
inline QString originalFilename(const KoResourceSP &preset)
{
    static const QRegularExpression version(QStringLiteral("\\.[0-9]+(?=\\.[^.]+$)"));
    QString filename=preset->filename();
    return filename.remove(version);
}

inline QString sourcePath(const KoResourceSP &preset)
{
    if(!preset || preset->resourceType().first!=QStringLiteral("paintoppresets"))return {};
    const QString root=KoResourcePaths::getApplicationRoot()+QStringLiteral("/share/krita/");
    const QFileInfo storage(preset->storageLocation());
    QString path;
    if(storage.suffix().compare(QStringLiteral("bundle"),Qt::CaseInsensitive)==0) {
        path=root+QStringLiteral("bundles/")+storage.fileName();
    } else if(preset->storageLocation().isEmpty() ||
              QFileInfo(KoResourcePaths::getAppDataLocation())==storage) {
        path=root+QStringLiteral("paintoppresets/")+originalFilename(preset);
    }
    return QFileInfo::exists(path)?path:QString();
}

}
