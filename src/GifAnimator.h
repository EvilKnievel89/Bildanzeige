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

class ImageDocument;

// Wiedergabe animierter GIFs.
//
// Ein GIF-Frame ist kein fertiges Bild, sondern ein Teilrechteck, das an seiner
// Position auf eine Leinwand in Größe von /logscrdesc gehört -- mit
// Transparenz und einer Aufräumregel (Disposal), die bestimmt, was davon für
// den nächsten Frame stehenbleibt. Einzelne Frames direkt anzuzeigen ergäbe
// springende Fragmente.
//
// Komponiert wird deshalb immer fortlaufend auf einer laufenden Leinwand; das
// ist die einzige Reihenfolge, in der die Aufräumregeln überhaupt definiert
// sind. Ein Rücksprung heißt: von Frame 0 an nachspielen.
//
// Fertige Leinwände werden dabei nebenbei aufgehoben, solange das Budget
// reicht. Der erste Durchlauf komponiert also, ab dem zweiten wird nur noch
// abgerufen -- das Öffnen bleibt schnell, und es wird nur so viel Speicher
// belegt, wie auch wirklich gezeigt wurde. Reicht das Budget nicht, kostet ein
// Rücksprung dauerhaft einen Neuaufbau.
class GifAnimator
{
public:
    // Liefert false, wenn es kein animiertes GIF ist -- dann bleibt es beim
    // gewöhnlichen Seitenbetrieb.
    bool Load(IWICImagingFactory* factory, const ImageDocument& document);
    void Reset();

    bool IsActive() const { return !frames_.empty(); }
    UINT FrameCount() const { return static_cast<UINT>(frames_.size()); }
    UINT LoopCount() const { return loopCount_; }      // 0 == endlos
    UINT DelayMs(UINT index) const;
    bool Caching() const { return caching_; }

    // Die fertig komponierte Leinwand für diesen Frame.
    ComPtr<IWICBitmapSource> Compose(UINT index, std::wstring& error);

private:
    struct Frame
    {
        UINT left;
        UINT top;
        UINT width;
        UINT height;
        UINT delayMs;
        UINT disposal;   // 0/1 stehenlassen, 2 auf Hintergrund, 3 auf vorigen Stand
    };

    UINT ClipWidth(const Frame& frame) const;
    UINT ClipHeight(const Frame& frame) const;

    bool EnsureCanvas();
    bool DrawFrame(UINT index, std::wstring& error);
    bool ApplyDisposal(UINT index);
    bool ClearArea(IWICBitmap* bitmap, UINT left, UINT top, UINT width, UINT height) const;
    ComPtr<IWICBitmap> CloneCanvas() const;

    ComPtr<IWICImagingFactory> factory_;

    // Zeigt auf das Mitglied von MainWindow und bleibt damit gültig; die
    // Frames werden bei Bedarf über ImageDocument nachgeladen, statt sie alle
    // dekodiert vorzuhalten.
    const ImageDocument* document_ = nullptr;

    std::vector<Frame> frames_;
    std::vector<ComPtr<IWICBitmap>> cache_;   // leer, wenn über dem Budget
    ComPtr<IWICBitmap> canvas_;
    ComPtr<IWICBitmap> backup_;               // Sicherung für Disposal 3
    UINT canvasWidth_ = 0;
    UINT canvasHeight_ = 0;
    UINT loopCount_ = 0;
    int composed_ = -1;                       // Frame, der auf der Leinwand steht
    bool caching_ = false;
};
