/*
 * SPDX-FileCopyrightText: 1998, 1999 Torben Weis <weis@kde.org>
 * SPDX-FileCopyrightText: 2012 Boudewijn Rempt <boud@valdyas.org>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 *
 * Android Krita engine setup, outside the desktop KisApplication.
 * This is an engine component usable by a future independent entrypoint.
 */
#pragma once

class MobileKritaEngineInitialization final
{
public:
    static void addResourceTypes();
    static void loadPlugins();
    static bool registerResources();
};
