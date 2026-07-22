#pragma once

#include "Common.h"

#include <wincodec.h>

#include <string>
#include <vector>

// Die anzeigbaren Dateien im selben Ordner, natuerlich sortiert.
//
// Die Liste ist eine Momentaufnahme: sie entsteht beim Oeffnen und wird erst
// wieder eingelesen, wenn eine Datei aus einem anderen Ordner kommt. Einen
// Ordner mit mehreren tausend Dateien bei jedem Bildwechsel neu einzulesen
// waere spuerbar, und waehrend man Bilder ansieht aendert er sich in aller
// Regel nicht. Was inzwischen doch verschwunden ist, faellt beim Ansteuern auf
// und wird uebersprungen -- siehe Drop().
class FolderNavigator
{
public:
    // Merkt sich die Datei als die aktuelle. Der Ordner wird nur eingelesen,
    // wenn es ein anderer ist als der zuletzt eingelesene.
    void Track(IWICImagingFactory* factory, const std::wstring& path);
    void Clear();

    // Erste anzeigbare Datei eines Ordners -- fuer den Fall, dass ein Ordner
    // uebergeben oder auf das Fenster gezogen wird. Leer, wenn keine drin ist.
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
    std::wstring folder_;                    // mit abschliessendem Trennzeichen
    size_t index_ = 0;
    bool scanned_ = false;
};
