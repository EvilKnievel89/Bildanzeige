#include "RenderView.h"

#include "Toolbar.h"

#include <algorithm>

namespace
{
    // Hintergrund der Bildfläche.
    constexpr UINT32 kBackground = 0x1E1E1E;
}

HRESULT RenderView::Initialize(HWND hwnd, IWICImagingFactory* wic)
{
    hwnd_ = hwnd;
    wic_ = wic;

    const HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf());
    if (FAILED(hr))
        return hr;

    return CreateDeviceResources();
}

void RenderView::Shutdown()
{
    DiscardDeviceResources();
    factory_.Reset();
    wic_.Reset();
    hwnd_ = nullptr;
}

HRESULT RenderView::CreateDeviceResources()
{
    if (target_ || !factory_ || !hwnd_)
        return S_OK;

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const D2D1_SIZE_U size = D2D1::SizeU(
        static_cast<UINT32>(std::max<LONG>(rc.right - rc.left, 1)),
        static_cast<UINT32>(std::max<LONG>(rc.bottom - rc.top, 1)));

    // DPI fest auf 96 -> ein DIP entspricht genau einem Pixel. Für einen
    // Bildbetrachter ist das die ehrlichste Basis; die DPI-Skalierung der
    // Bedienelemente wird später explizit gerechnet.
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
    props.dpiX = 96.0f;
    props.dpiY = 96.0f;

    // Die Bitmap kehrt hier nicht von selbst zurück: sie wird vom Aufrufer
    // neu angefordert, sobald er HasImage() als falsch vorfindet.
    return factory_->CreateHwndRenderTarget(
        props, D2D1::HwndRenderTargetProperties(hwnd_, size), &target_);
}

void RenderView::DiscardDeviceResources()
{
    bitmap_.Reset();
    target_.Reset();
}

UINT RenderView::MaxBitmapSize() const
{
    return target_ ? target_->GetMaximumBitmapSize() : 0;
}

HRESULT RenderView::SetImage(IWICBitmapSource* source, UINT nominalWidth, UINT nominalHeight)
{
    nominalWidth_ = nominalWidth;
    nominalHeight_ = nominalHeight;
    const HRESULT hr = Upload(source);

    // Jede neue Seite beginnt eingepasst. Den Zoom der Vorgängerseite
    // beizubehalten wäre bei unterschiedlich großen Seiten ein Blindflug.
    state_.Reset();
    return hr;
}

HRESULT RenderView::UpdateImage(IWICBitmapSource* source)
{
    const D2D1_SIZE_F before = state_.ImageSize();

    nominalWidth_ = 0;   // Eine GIF-Leinwand liegt stets vollständig vor.
    nominalHeight_ = 0;
    const HRESULT hr = Upload(source);

    // Bei einer Animation wechselt nur der Inhalt der Leinwand; wer in ein
    // Detail hineingezoomt hat, soll es im nächsten Einzelbild wiederfinden.
    // Ändert sich dagegen die Größe, wären Maßstab und Ausschnitt auf
    // etwas anderes bezogen -- dann wird eingepasst.
    const D2D1_SIZE_F after = state_.ImageSize();
    if (after.width != before.width || after.height != before.height)
        state_.Reset();
    return hr;
}

void RenderView::ClearImage()
{
    nominalWidth_ = 0;
    nominalHeight_ = 0;
    bitmap_.Reset();
    state_.SetImageSize(D2D1::SizeF(0.0f, 0.0f));
    state_.Reset();
}

HRESULT RenderView::Upload(IWICBitmapSource* source)
{
    bitmap_.Reset();
    if (!target_ || source == nullptr)
        return E_FAIL;

    UINT width = 0;
    UINT height = 0;
    HRESULT hr = source->GetSize(&width, &height);
    if (FAILED(hr))
        return hr;

    ComPtr<IWICBitmapSource> upload = source;

    // Der Hintergrund-Thread hat die Texturgrenze bereits berücksichtigt --
    // er kennt sie über MaxBitmapSize(). Diese Rechnung bleibt trotzdem
    // stehen: die Grenze wird vor dem Auftrag abgefragt, und bis das Ergebnis
    // eintrifft, kann das Fenster auf einer anderen Grafikkarte gelandet sein.
    const UINT maxDimension = target_->GetMaximumBitmapSize();
    if (width > maxDimension || height > maxDimension)
    {
        const double factor =
            static_cast<double>(maxDimension) / static_cast<double>(std::max(width, height));
        const UINT scaledWidth = std::max<UINT>(1, static_cast<UINT>(width * factor));
        const UINT scaledHeight = std::max<UINT>(1, static_cast<UINT>(height * factor));

        ComPtr<IWICBitmapScaler> scaler;
        hr = wic_->CreateBitmapScaler(&scaler);
        if (FAILED(hr))
            return hr;

        hr = scaler->Initialize(source, scaledWidth, scaledHeight,
                                WICBitmapInterpolationModeFant);
        if (FAILED(hr))
            return hr;

        hr = scaler.As(&upload);
        if (FAILED(hr))
            return hr;
    }

    hr = target_->CreateBitmapFromWicBitmap(upload.Get(), nullptr,
                                            bitmap_.ReleaseAndGetAddressOf());
    if (FAILED(hr))
        return hr;

    // Die Ansicht rechnet in den Maßen der Datei, nicht in denen der Textur.
    // Musste für die Texturgrenze verkleinert werden, wird das Bitmap beim
    // Zeichnen wieder auseinandergezogen; "Originalgröße" bedeutet dann
    // weiterhin die Größe, die in der Datei steht -- nur eben aus weniger
    // Pixeln aufgebaut, denn mehr nimmt das Gerät nicht an.
    const D2D1_SIZE_F texture = bitmap_->GetSize();
    state_.SetImageSize(nominalWidth_ > 0 && nominalHeight_ > 0
                            ? D2D1::SizeF(static_cast<float>(nominalWidth_),
                                          static_cast<float>(nominalHeight_))
                            : texture);
    return S_OK;
}

void RenderView::Resize(UINT width, UINT height)
{
    if (target_)
        target_->Resize(D2D1::SizeU(std::max<UINT>(width, 1), std::max<UINT>(height, 1)));
    UpdateViewport(static_cast<float>(width), static_cast<float>(height));
}

void RenderView::SetToolbarHeight(float height)
{
    toolbarHeight_ = height;
    if (target_)
    {
        const D2D1_SIZE_F size = target_->GetSize();
        UpdateViewport(size.width, size.height);
    }
}

void RenderView::UpdateViewport(float width, float height)
{
    state_.SetViewport(D2D1::SizeF(width, std::max(height - toolbarHeight_, 0.0f)));
}

HRESULT RenderView::Render(Toolbar* toolbar)
{
    if (!target_)
        return E_FAIL;

    target_->BeginDraw();
    target_->Clear(D2D1::ColorF(kBackground));

    if (bitmap_)
    {
        const D2D1_SIZE_F client = target_->GetSize();
        UpdateViewport(client.width, client.height);

        // Bei 90 und 270 Grad tauschen Breite und Höhe die Rollen: die Ansicht
        // rechnet mit der gedrehten Größe, gezeichnet wird das ungedrehte
        // Bitmap in ein entsprechend zurückgetauschtes Rechteck.
        const D2D1_RECT_F dest = state_.DestRect();
        const int rotation = state_.Rotation();
        const bool swapped = (rotation % 2) != 0;

        const float cx = (dest.left + dest.right) * 0.5f;
        const float cy = (dest.top + dest.bottom) * 0.5f;
        const float shownWidth = dest.right - dest.left;
        const float shownHeight = dest.bottom - dest.top;
        const float drawWidth = swapped ? shownHeight : shownWidth;
        const float drawHeight = swapped ? shownWidth : shownHeight;

        const D2D1_RECT_F drawRect = D2D1::RectF(cx - drawWidth * 0.5f, cy - drawHeight * 0.5f,
                                                 cx + drawWidth * 0.5f, cy + drawHeight * 0.5f);

        // Beim Verkleinern wird geglättet, sonst rauschen feine Raster.
        // Oberhalb der Originalgröße dagegen bleiben die Pixel hart: bei
        // einem Scan will man sehen, was dasteht, nicht ein weichgezeichnetes
        // Mittel daraus.
        //
        // Gemessen wird an der Textur, nicht am Maßstab der Ansicht: musste
        // das Bild für die Texturgrenze verkleinert werden, gibt es dort keine
        // echten Pixel mehr zu zeigen, und harte Kanten würden nur die Stufen
        // des Herunterrechnens vergrößern.
        const D2D1_SIZE_F texture = bitmap_->GetSize();
        const bool full = texture.width >= state_.ImageSize().width;
        const D2D1_BITMAP_INTERPOLATION_MODE mode =
            (full && texture.width > 0.0f && drawWidth > texture.width)
                ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
                : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;

        target_->SetTransform(D2D1::Matrix3x2F::Rotation(
            static_cast<float>(rotation) * 90.0f, D2D1::Point2F(cx, cy)));
        target_->DrawBitmap(bitmap_.Get(), drawRect, 1.0f, mode);
        target_->SetTransform(D2D1::Matrix3x2F::Identity());
    }

    if (toolbar != nullptr)
        toolbar->Draw(target_.Get());

    return target_->EndDraw();
}
