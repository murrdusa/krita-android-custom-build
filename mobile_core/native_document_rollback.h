/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Krita view attachment may synchronously destroy its document when
 * KisPart::removeView() closes the last view. Never blindly remove the
 * document again after attachNewDocument() fails.
 *
 * This generic Qt-only identity guard is exercised without linking
 * Krita, and is shared by both native and desktop document backends.
 */
#pragma once
#if defined(Q_OS_ANDROID) || defined(KRITA_MOBILE_QT_TEST)
#include <QPointer>
#include <utility>

class MobileFailedDocumentAttach final
{
public:
    template<class T,class Registered,class Remove>
    static bool rollback(const QPointer<T> &original,
                         Registered &&registered,Remove &&remove)
    {
        if(!original)return false;
        T *document=original.data();
        if(!registered(document) || !original)return false;
        // Callers may remove synchronously through KisPart. Never touch
        // the raw identity again once its remover has been invoked.
        remove(document);
        return true;
    }

    // A Krita observer can unregister an imported document without
    // destroying its QObject. After a failed view attachment, that
    // unowned image would retain its layers and pixel buffers. Only
    // dispose of an identity confirmed absent from the live registry;
    // never delete a still-registered document or an already dead one.
    template<class T,class Registered>
    static bool disposeUnregistered(const QPointer<T> &original,
                                    Registered &&registered)
    {
        if(!original)return false;
        const bool inRegistry=registered(original.data());
        if(!original || inRegistry)return false;
        delete original.data();
        return true;
    }
};
#endif
