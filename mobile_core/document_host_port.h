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
    // Return true only when Krita has accepted the document and its
    // native view. A false return must leave no retained view/ownership:
    // the document service then removes the registered KisDocument.
    virtual bool attachNewDocument(KisDocument *document) const = 0;
    virtual bool openFile(const QString &path) const = 0;
    virtual bool save(KisDocument *document,bool saveAs) const = 0;
    virtual bool prepareHomeClose(KisDocument *) const {return false;}
    virtual bool exportCopy(KisDocument *) const {return false;}
};
#endif // Q_OS_ANDROID
