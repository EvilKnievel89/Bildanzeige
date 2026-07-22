#pragma once

#include "Common.h"
#include "ViewState.h"

#include <d2d1.h>
#include <wincodec.h>

class Toolbar;

// Direct2D-Darstellung des Bildes im Fenster.
//
// Die WIC-Quelle wird nach dem Hochladen fallengelassen. Sie festzuhalten waere
// bequem -- geht das Geraet verloren (GPU-Reset, Treiberwechsel), liesse sich
// die ID2D1Bitmap ohne erneutes Lesen wiederherstellen -- kostete bei einem
// 64-Megapixel-Bild aber dauerhaft eine viertel Gigabyte fuer einen Fall, der
// jahrelang ausbleiben kann. Nach einem Verlust ist HasImage() falsch; der
// Aufrufer laesst das Bild dann neu holen.
class RenderView
{
public:
    HRESULT Initialize(HWND hwnd, IWICImagingFactory* wic);
    void Shutdown();

    // Neues Bild: die Ansicht faengt eingepasst und ungezoomt an.
    //
    // nominalWidth/Height ist die Groesse, die in der Datei steht. Musste das
    // Bild fuer die Texturgrenze der GPU verkleinert werden, ist die Quelle
    // kleiner -- Massstab und "Originalgroesse" beziehen sich trotzdem auf die
    // Zahlen der Datei, sonst zeigte "100 %" bei einem Grossformat etwas
    // anderes als die Originalgroesse. 0 heisst: so gross wie die Quelle.
    HRESULT SetImage(IWICBitmapSource* source, UINT nominalWidth = 0, UINT nominalHeight = 0);

    // Groesste Kantenlaenge, die das Geraet als Textur annimmt (0 = unbekannt).
    UINT MaxBitmapSize() const;

    // Gleiche Flaeche, neuer Inhalt -- das naechste Einzelbild einer Animation.
    // Massstab, Ausschnitt und Drehung bleiben stehen.
    HRESULT UpdateImage(IWICBitmapSource* source);

    void ClearImage();

    void Resize(UINT width, UINT height);

    // Hoehe der Leiste am unteren Rand; das Bild wird nur darueber eingepasst.
    void SetToolbarHeight(float height);

    bool HasImage() const { return bitmap_ != nullptr; }

    ViewState& View() { return state_; }
    const ViewState& View() const { return state_; }

    // Liefert D2DERR_RECREATE_TARGET, wenn die Geraeteressourcen verloren sind.
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
