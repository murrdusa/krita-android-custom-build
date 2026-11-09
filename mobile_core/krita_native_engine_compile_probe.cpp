/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Build-only Krita 6.0.4 integration gate.
 * Added to the REAL kritaui Android target, so the compiler parses the
 * native view factory, engine manager, document save adapter and host.
 * This file does not create any windows, actions, documents or APKs.
 * Existing desktop-backed Mobile Studio behavior remains unchanged.
 */
#ifdef Q_OS_ANDROID
#include <type_traits>
#include <KoCanvasSupervisor.h>
#include "krita_standalone_documents.h"
#include "krita_standalone_runtime.h"
#include "krita_native_view_factory.h"

static_assert(std::is_base_of<KoCanvasSupervisor,
              MobileLightweightCanvasHost>::value,
              "Independent host must satisfy Krita's canvas supervisor");
static_assert(std::is_base_of<MobileViewHostPort,
              MobileNativeViewStack>::value,
              "Native document root must expose real view identities");
static_assert(std::is_base_of<MobileDocumentHostPort,
              MobileKritaStandaloneDocuments>::value,
              "Standalone documents must use Krita's document port");

// No runtime entrypoint: inclusion itself forces the compiler to type-check
// the inline Krita API calls of the future independent painting host.
#endif
