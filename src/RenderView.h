// Bildanzeige -- Copyright (C) 2026 EvilKnievel89
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Freie Software unter der GNU GPL, Version 3 oder einer späteren: weitergeben
// und verändern ist erlaubt, solange Quelltext und Lizenz mitgehen. Ohne jede
// Gewährleistung. Wortlaut in LICENSE, Erklärung in LIZENZ.md.

#pragma once

#include "Common.h"
#include "ViewState.h"

#include <d2d1.h>
#include <wincodec.h>

class Toolbar;

// Direct2D-Darstellung des Bildes im Fenster.
//
// Die WIC-Quelle wird nach dem Hochladen fallengelassen. Sie festzuhalten wäre
// bequem -- geht das Gerät verloren (GPU-Reset, Treiberwechsel), ließe sich
// die ID2D1Bitmap ohne erneutes Lesen wiederherstellen -- kostete bei einem
// 64-Megapixel-Bild aber dauerhaft eine viertel Gigabyte für einen Fall, der
// jahrelang ausbleiben kann. Nach einem Verlust ist HasImage() falsch; der
// Aufrufer lässt das Bild dann neu holen.
class RenderView
{
public:
    HRESULT Initialize(HWND hwnd, IWICImagingFactory* wic);
    void Shutdown();

    // Neues Bild: die Ansicht fängt eingepasst und ungezoomt an.
    //
    // nominalWidth/Height ist die Größe, die in der Datei steht. Musste das
    // Bild für die Texturgrenze der GPU verkleinert werden, ist die Quelle
    // kleiner -- Maßstab und "Originalgröße" beziehen sich trotzdem auf die
    // Zahlen der Datei, sonst zeigte "100 %" bei einem Großformat etwas
    // anderes als die Originalgröße. 0 heißt: so groß wie die Quelle.
    HRESULT SetImage(IWICBitmapSource* source, UINT nominalWidth = 0, UINT nominalHeight = 0);

    // Größte Kantenlänge, die das Gerät als Textur annimmt (0 = unbekannt).
    UINT MaxBitmapSize() const;

    // Gleiche Fläche, neuer Inhalt -- das nächste Einzelbild einer Animation.
    // Maßstab, Ausschnitt und Drehung bleiben stehen.
    HRESULT UpdateImage(IWICBitmapSource* source);

    void ClearImage();

    void Resize(UINT width, UINT height);

    // Höhe der Leiste am unteren Rand; das Bild wird nur darüber eingepasst.
    void SetToolbarHeight(float height);

    bool HasImage() const { return bitmap_ != nullptr; }

    ViewState& View() { return state_; }
    const ViewState& View() const { return state_; }

    // Liefert D2DERR_RECREATE_TARGET, wenn die Geräteressourcen verloren sind.
    HRESULT Render(Toolbar* toolbar);

    void DiscardDeviceResources();
    HRESULT CreateDeviceResources();

    ID2D1Factory* Factory() const { return factory_.Get(); }

private:
    HRESULT Upload(IWICBitmapSource* source);
    void UpdateViewport(float width, float height);

    HWND hwnd_ = nullptr;
    float toolbarHeight_ = 0.0f;
    UINT nominalWidth_ = 0;
    UINT nominalHeight_ = 0;
    ViewState state_;
    ComPtr<IWICImagingFactory> wic_;
    ComPtr<ID2D1Factory> factory_;
    ComPtr<ID2D1HwndRenderTarget> target_;
    ComPtr<ID2D1Bitmap> bitmap_;
};
