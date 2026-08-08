// Bildanzeige -- Copyright (C) 2026 EvilKnievel89
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Freie Software unter der GNU GPL, Version 3 oder einer späteren: weitergeben
// und verändern ist erlaubt, solange Quelltext und Lizenz mitgehen. Ohne jede
// Gewährleistung. Wortlaut in LICENSE, Erklärung in LIZENZ.md.

#pragma once

#include "Common.h"

#include <wincodec.h>

#include <string>
#include <vector>

// Die anzeigbaren Dateien im selben Ordner, natürlich sortiert.
//
// Die Liste ist eine Momentaufnahme: sie entsteht beim Öffnen und wird erst
// wieder eingelesen, wenn eine Datei aus einem anderen Ordner kommt. Einen
// Ordner mit mehreren tausend Dateien bei jedem Bildwechsel neu einzulesen
// wäre spürbar, und während man Bilder ansieht ändert er sich in aller
// Regel nicht. Was inzwischen doch verschwunden ist, fällt beim Ansteuern auf
// und wird übersprungen -- siehe Drop().
class FolderNavigator
{
public:
    // Merkt sich die Datei als die aktuelle. Der Ordner wird nur eingelesen,
    // wenn es ein anderer ist als der zuletzt eingelesene.
    void Track(IWICImagingFactory* factory, const std::wstring& path);
    void Clear();

    // Erste anzeigbare Datei eines Ordners -- für den Fall, dass ein Ordner
    // übergeben oder auf das Fenster gezogen wird. Leer, wenn keine drin ist.
    std::wstring FirstIn(IWICImagingFactory* factory, const std::wstring& folder);

    size_t Count() const { return files_.size(); }
    size_t Index() const { return index_; }

    bool HasNeighbour(int delta) const;
    std::wstring Neighbour(int delta) const;   // voller Pfad, leer am Rand

    // Nimmt eine verschwundene Datei aus der Liste, ohne die aktuelle Stelle
    // zu verlieren.
    void Drop(const std::wstring& path);

private:
    void Scan(IWICImagingFactory* factory, const std::wstring& folder);
    void EnsureExtensions(IWICImagingFactory* factory);
    void AddExtensions(const std::wstring& list);
    bool Accepts(const wchar_t* name) const;

    std::vector<std::wstring> extensions_;   // klein geschrieben, mit Punkt
    std::vector<std::wstring> files_;        // nur Dateinamen
    std::wstring folder_;                    // mit abschließendem Trennzeichen
    size_t index_ = 0;
    bool scanned_ = false;
};
