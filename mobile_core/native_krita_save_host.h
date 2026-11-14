/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Save-on-close hook for native Krita KisView instances hosted outside
 * KisMainWindow. The paint and file engines remain Krita-owned.
 * A missing or destroyed host refuses Save rather than dereferencing
 * a legacy desktop main window or silently discarding a document.
 */
#pragma once
#if defined(Q_OS_ANDROID) || defined(KRITA_MOBILE_QT_TEST)
#include <QObject>
#include <QPointer>
#include <QSharedPointer>
#include "document_host_port.h"

class KisDocument;

class MobileKritaNativeSaveHost : public QObject
{
public:
    using QObject::QObject;
    virtual ~MobileKritaNativeSaveHost() = default;
    virtual bool save(KisDocument *document,bool saveAs) = 0;
    virtual bool saving() const = 0;
};

// A replaceable adapter instead of reimplementing Krita's KRA writer.
// A KisView only holds this object through QPointer. The document port
// may itself be a lightweight implementation or the desktop rollback.
class MobileKritaNativeSavePortAdapter final : public MobileKritaNativeSaveHost
{
public:
    explicit MobileKritaNativeSavePortAdapter(
        QSharedPointer<MobileDocumentHostPort> documents,
        QObject *parent=nullptr)
        : MobileKritaNativeSaveHost(parent),m_documents(std::move(documents)) {}

    bool save(KisDocument *document,bool saveAs) override
    {
        if(m_saving || !document)return false;
        // A native Save may close the last canvas and synchronously
        // destroy this adapter. Hold the backend alive independently and
        // never run a scoped rollback against a deleted QObject.
        const QSharedPointer<MobileDocumentHostPort> documents=m_documents;
        if(!documents || !documents->available())return false;
        QPointer<MobileKritaNativeSavePortAdapter> self(this);
        m_saving=true;
        const bool result=documents->save(document,saveAs);
        if(self)self->m_saving=false;
        return result;
    }

    bool saving() const override {return m_saving;}

private:
    QSharedPointer<MobileDocumentHostPort> m_documents;
    bool m_saving=false;
};
#endif
