// Bildanzeige -- Copyright (C) 2026 EvilKnievel89
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Freie Software unter der GNU GPL, Version 3 oder einer späteren: weitergeben
// und verändern ist erlaubt, solange Quelltext und Lizenz mitgehen. Ohne jede
// Gewährleistung. Wortlaut in LICENSE, Erklärung in LIZENZ.md.

#pragma once

#include "Common.h"

// Kleines Fenster für die Einstellungen, erreichbar über das Zahnrad ganz
// rechts in der Icon-Leiste.
//
// Darin steht bislang eine einzige Sache: ob Windows den klassischen
// Druckdialog zeigen soll (PrintDialogSetting.h). Der Stand kommt beim Öffnen
// aus der Registrierung, geschrieben wird erst mit "OK" -- "Abbrechen" und Esc
// lassen alles, wie es war.
//
// Modal wie die Seitenansicht und die Dateizuordnungen: solange es steht, ist
// das Hauptfenster gesperrt.
void ShowSettingsDialog(HWND owner);
