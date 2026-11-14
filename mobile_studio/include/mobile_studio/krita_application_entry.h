/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Single native Android drawing window. No desktop eligibility checks.
 */
#pragma once
#include <QtGlobal>
#ifdef Q_OS_ANDROID
class MobileKritaApplicationEntry final
{
public:
    static bool start();
};
#endif
