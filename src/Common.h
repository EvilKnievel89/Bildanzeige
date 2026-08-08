// Bildanzeige -- Copyright (C) 2026 EvilKnievel89
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Freie Software unter der GNU GPL, Version 3 oder einer späteren: weitergeben
// und verändern ist erlaubt, solange Quelltext und Lizenz mitgehen. Ohne jede
// Gewährleistung. Wortlaut in LICENSE, Erklärung in LIZENZ.md.

#pragma once

#include <windows.h>
#include <wrl/client.h>

#include <string>

template <class T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

// Obergrenze für dekodierte Bilder, in Bildpunkten. Die Maße stehen im
// Dateikopf und sind damit bloße Behauptung; eingelöst würden sie erst beim
// Dekodieren, mit vier Byte je Bildpunkt. Eine präparierte Datei kann so mit
// wenigen Kilobyte viele Gigabyte Speicher bestellen -- und auf einem
// Terminalserver wäre das der Speicher aller Sitzungen. 250 Megapixel lassen
// jeden echten Scan durch (A2 bei 600 dpi liegt bei 139) und schneiden nur
// solche Bestellungen ab.
constexpr unsigned long long kMaxImagePixels = 250'000'000ull;

// Systemmeldung zu einem HRESULT, für Fehlertexte in der UI.
std::wstring FormatHResult(HRESULT hr);
