// Bildanzeige -- Copyright (C) 2026 EvilKnievel89
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Freie Software unter der GNU GPL, Version 3 oder einer späteren: weitergeben
// und verändern ist erlaubt, solange Quelltext und Lizenz mitgehen. Ohne jede
// Gewährleistung. Wortlaut in LICENSE, Erklärung in LIZENZ.md.

#pragma once

#include "Common.h"
#include "FileAssociation.h"   // RegistryScope

#include <string>

// Der klassische Druckdialog von Windows.
//
// Windows 11 hat den Druckdialog für alle Win32-Anwendungen gegen einen eigenen
// ausgetauscht. Der alte kommt zurück, wenn unter
// Software\Microsoft\Print\UnifiedPrintDialog der Wert PreferLegacyPrintDialog
// auf 1 steht.
//
// Gelesen wird er von comdlg32.dll, und die läuft im Prozess der druckenden
// Anwendung -- nicht in einem Dienst und nicht im Explorer. Es gibt damit
// niemanden, der ihn für andere zwischenspeichern könnte: die Umstellung wirkt
// beim nächsten Druckdialog. Nachgemessen an einem Programm, das in einem
// einzigen Lauf dreimal PrintDlg aufruft und dazwischen den Wert umstellt --
// jeder Aufruf zeigte den Dialog, der in dem Augenblick eingetragen war.

// Der Zweig, den dieser Lauf beschreibt.
//
// HKEY_LOCAL_MACHINE nur dann, wenn erhöht gestartet wurde *und* der Wert dort
// bereits steht. Angelegt wird er dort nicht: ein Betrachter, der ungefragt
// einen rechnerweiten Eintrag hinterlässt, den vorher niemand gesetzt hat,
// entscheidet für alle Benutzer mit, die davon nichts wissen.
RegistryScope PrintDialogScope();

// Steht der Wert in diesem Zweig? Das ist nicht die Frage, was er sagt.
bool HasLegacyPrintDialog(RegistryScope scope);

// Was dort steht. Fehlt der Wert, gilt der neue Dialog -- so hält es Windows.
bool IsLegacyPrintDialogEnabled(RegistryScope scope);

// Trägt den Wunsch ein. Bei false steht in error, woran es lag.
bool SetLegacyPrintDialog(RegistryScope scope, bool enabled, std::wstring& error);
