/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Mobile document lifecycle host boundary. Krita engine documents and
 * format handling remain native; no desktop QMainWindow dependency.
 * A future lightweight Android host implements this interface instead
 * of rewriting the mobile document UI, views or save prompts.
 */
#pragma once
#if defined(Q_OS_ANDROID) || defined(KRITA_MOBILE_QT_TEST)
#include <QSharedPointer>
#include <QString>
class KisDocument;

class MobileDocumentHostPort
{
public:
    virtual ~MobileDocumentHostPort() = default;
    virtual bool available() const = 0;
    virtual bool attachNewDocument(KisDocument *document) const = 0;
    virtual bool openFile(const QString &path) const = 0;
    virtual bool save(KisDocument *document,bool saveAs) const = 0;
};
#endif // Q_OS_ANDROID
