#include "GifAnimator.h"

#include "ImageDocument.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>

namespace
{
    // Leinwand und Frames liegen in 32bppPBGRA -- vier Bytes je Pixel, Alpha
    // vormultipliziert. Dasselbe Format liefert ImageDocument::LoadFrame.
    constexpr UINT kBytes = 4;

    // Obergrenze für die aufgehobenen Leinwände. Darüber wird fortlaufend
    // komponiert; ein Rücksprung kostet dann einen Neuaufbau ab Frame 0.
    constexpr size_t kMaxCacheBytes = 256ull * 1024ull * 1024ull;

    UINT ReadUInt(IWICMetadataQueryReader* reader, const wchar_t* path, UINT fallback)
    {
        if (reader == nullptr)
            return fallback;

        PROPVARIANT value;
        PropVariantInit(&value);
        UINT result = fallback;
        if (SUCCEEDED(reader->GetMetadataByName(path, &value)))
        {
            switch (value.vt)
            {
            case VT_UI1: result = value.bVal;  break;
            case VT_UI2: result = value.uiVal; break;
            case VT_UI4: result = value.ulVal; break;
            default: break;
            }
        }
        PropVariantClear(&value);
        return result;
    }

    UINT ReadLoopCount(IWICMetadataQueryReader* reader)
    {
        // Ohne NETSCAPE2.0-Erweiterung sieht die Spezifikation einen einzigen
        // Durchlauf vor. Hier wird trotzdem endlos wiederholt: eine Animation,
        // die nach einem Durchgang stehenbleibt, sieht nach einem Fehler des
        // Betrachters aus, und anhalten kann der Benutzer jederzeit selbst.
        UINT loops = 0;
        if (reader == nullptr)
            return loops;

        PROPVARIANT name;
        PropVariantInit(&name);
        if (SUCCEEDED(reader->GetMetadataByName(L"/appext/Application", &name)) &&
            name.vt == (VT_UI1 | VT_VECTOR) && name.caub.cElems >= 11 &&
            std::memcmp(name.caub.pElems, "NETSCAPE2.0", 11) == 0)
        {
            PROPVARIANT data;
            PropVariantInit(&data);

            // WIC entfernt den Block-Terminator: es kommen 4 Bytes zurück, nicht
            // die geschriebenen 5. Die Länge muss also geprüft werden. Der
            // Zähler steht little-endian in Byte 2 und 3, 0 heißt endlos.
            if (SUCCEEDED(reader->GetMetadataByName(L"/appext/Data", &data)) &&
                data.vt == (VT_UI1 | VT_VECTOR) && data.caub.cElems >= 4)
            {
                loops = static_cast<UINT>(data.caub.pElems[2]) |
                        (static_cast<UINT>(data.caub.pElems[3]) << 8);
            }
            PropVariantClear(&data);
        }
        PropVariantClear(&name);
        return loops;
    }
}

bool GifAnimator::Load(IWICImagingFactory* factory, const ImageDocument& document)
{
    Reset();

    IWICBitmapDecoder* decoder = document.Decoder();
    if (factory == nullptr || decoder == nullptr || document.FrameCount() < 2)
        return false;

    GUID container{};
    if (FAILED(decoder->GetContainerFormat(&container)) || container != GUID_ContainerFormatGif)
        return false;

    ComPtr<IWICMetadataQueryReader> global;
    decoder->GetMetadataQueryReader(&global);   // fehlt sie, greifen die Rücklagen

    UINT canvasWidth = ReadUInt(global.Get(), L"/logscrdesc/Width", 0);
    UINT canvasHeight = ReadUInt(global.Get(), L"/logscrdesc/Height", 0);
    const UINT loops = ReadLoopCount(global.Get());

    std::vector<Frame> frames;
    frames.reserve(document.FrameCount());

    UINT spannedWidth = 0;
    UINT spannedHeight = 0;

    for (UINT i = 0; i < document.FrameCount(); ++i)
    {
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(i, &frame)))
            return false;

        Frame entry{};

        // Maßgeblich ist die tatsächliche Größe der Pixeldaten, nicht die
        // Angabe unter /imgdesc: weichen beide voneinander ab, läge beim
        // Kopieren sonst ein Überlauf nahe.
        if (FAILED(frame->GetSize(&entry.width, &entry.height)))
            return false;

        // Ein Frame oberhalb der Grenze aus kMaxImagePixels: dann kein
        // Abspielen, sondern Seitenbetrieb -- dort geht jedes Einzelbild durch
        // den Hintergrund-Thread, und der setzt dieselbe Grenze mit einer
        // Meldung durch, statt sie stillschweigend zu übergehen.
        if (static_cast<unsigned long long>(entry.width) * entry.height > kMaxImagePixels)
            return false;

        ComPtr<IWICMetadataQueryReader> meta;
        frame->GetMetadataQueryReader(&meta);
        entry.left = ReadUInt(meta.Get(), L"/imgdesc/Left", 0);
        entry.top = ReadUInt(meta.Get(), L"/imgdesc/Top", 0);
        entry.disposal = ReadUInt(meta.Get(), L"/grctlext/Disposal", 0);

        // Anzeigedauer in 1/100 s. 0 und 1 stehen in freier Wildbahn massenhaft
        // für "so schnell wie möglich" und ergäben eine Vollgas-Schleife; wie
        // in Browsern werden sie auf 100 ms angehoben.
        UINT delay = ReadUInt(meta.Get(), L"/grctlext/Delay", 0);
        if (delay < 2)
            delay = 10;
        entry.delayMs = delay * 10;

        spannedWidth = std::max(spannedWidth, entry.left + entry.width);
        spannedHeight = std::max(spannedHeight, entry.top + entry.height);
        frames.push_back(entry);
    }

    // Ohne brauchbares /logscrdesc bleibt nur die Hülle aller Frames. Steht die
    // Angabe dagegen da, gilt sie -- überstehende Frames werden beschnitten,
    // so wie es die Spezifikation vorsieht.
    if (canvasWidth == 0 || canvasHeight == 0)
    {
        canvasWidth = spannedWidth;
        canvasHeight = spannedHeight;
    }
    if (canvasWidth == 0 || canvasHeight == 0)
        return false;

    // Auch die Leinwand ist nur eine Behauptung aus /logscrdesc. Der Rückzug
    // in den Seitenbetrieb greift hier genauso wie bei einem zu großen Frame.
    if (static_cast<unsigned long long>(canvasWidth) * canvasHeight > kMaxImagePixels)
        return false;

    const size_t bytes =
        static_cast<size_t>(canvasWidth) * canvasHeight * kBytes * frames.size();

    factory_ = factory;
    document_ = &document;
    frames_ = std::move(frames);
    canvasWidth_ = canvasWidth;
    canvasHeight_ = canvasHeight;
    loopCount_ = loops;
    caching_ = bytes <= kMaxCacheBytes;
    if (caching_)
        cache_.resize(frames_.size());
    return true;
}

void GifAnimator::Reset()
{
    factory_.Reset();
    document_ = nullptr;
    frames_.clear();
    cache_.clear();
    canvas_.Reset();
    backup_.Reset();
    canvasWidth_ = 0;
    canvasHeight_ = 0;
    loopCount_ = 0;
    composed_ = -1;
    caching_ = false;
}

UINT GifAnimator::DelayMs(UINT index) const
{
    return index < frames_.size() ? frames_[index].delayMs : 100u;
}

UINT GifAnimator::ClipWidth(const Frame& frame) const
{
    if (frame.left >= canvasWidth_)
        return 0;
    return std::min(frame.width, canvasWidth_ - frame.left);
}

UINT GifAnimator::ClipHeight(const Frame& frame) const
{
    if (frame.top >= canvasHeight_)
        return 0;
    return std::min(frame.height, canvasHeight_ - frame.top);
}

bool GifAnimator::EnsureCanvas()
{
    if (canvas_)
        return true;

    if (FAILED(factory_->CreateBitmap(canvasWidth_, canvasHeight_,
                                      GUID_WICPixelFormat32bppPBGRA,
                                      WICBitmapCacheOnLoad, &canvas_)))
    {
        return false;
    }

    // WIC sagt nicht zu, dass eine frische Bitmap genullt ist -- ohne dieses
    // Leeren stünde womöglich Speichermüll hinter den transparenten Stellen.
    composed_ = -1;
    return ClearArea(canvas_.Get(), 0, 0, canvasWidth_, canvasHeight_);
}

bool GifAnimator::ClearArea(IWICBitmap* bitmap, UINT left, UINT top,
                            UINT width, UINT height) const
{
    if (bitmap == nullptr)
        return false;
    if (width == 0 || height == 0)
        return true;

    WICRect rect = { static_cast<INT>(left), static_cast<INT>(top),
                     static_cast<INT>(width), static_cast<INT>(height) };
    ComPtr<IWICBitmapLock> lock;
    if (FAILED(bitmap->Lock(&rect, WICBitmapLockWrite, &lock)))
        return false;

    UINT stride = 0;
    UINT size = 0;
    BYTE* data = nullptr;
    if (FAILED(lock->GetStride(&stride)) || FAILED(lock->GetDataPointer(&size, &data)))
        return false;

    // Zeilenweise, weil der Zeilenabstand größer sein kann als die Breite des
    // gesperrten Ausschnitts.
    for (UINT y = 0; y < height; ++y)
        std::memset(data + static_cast<size_t>(y) * stride, 0,
                    static_cast<size_t>(width) * kBytes);
    return true;
}

ComPtr<IWICBitmap> GifAnimator::CloneCanvas() const
{
    ComPtr<IWICBitmap> copy;
    if (!canvas_ || !factory_)
        return nullptr;
    if (FAILED(factory_->CreateBitmapFromSource(canvas_.Get(), WICBitmapCacheOnLoad, &copy)))
        return nullptr;
    return copy;
}

bool GifAnimator::ApplyDisposal(UINT index)
{
    const Frame& frame = frames_[index];
    switch (frame.disposal)
    {
    case 2:
        // "Restore to background": die Fläche des Frames wird wieder frei. Der
        // Hintergrundindex aus /logscrdesc bleibt dabei außen vor -- wie in
        // Browsern wird durchsichtig geräumt, sonst stünde bei einem
        // transparenten GIF plötzlich eine Farbfläche im Bild.
        return ClearArea(canvas_.Get(), frame.left, frame.top,
                         ClipWidth(frame), ClipHeight(frame));

    case 3:
        // "Restore to previous": der vor dem Frame gesicherte Stand. Fehlt die
        // Sicherung, bleibt die Leinwand stehen -- das ist immer noch näher am
        // Gemeinten als sie zu leeren.
        if (backup_)
        {
            canvas_ = backup_;
            backup_.Reset();
        }
        return true;

    default:
        return true;   // 0 und 1: stehenlassen
    }
}

bool GifAnimator::DrawFrame(UINT index, std::wstring& error)
{
    const Frame& frame = frames_[index];

    // Disposal 3 verlangt den Stand *vor* diesem Frame. Die Sicherung muss
    // deshalb hier entstehen und nicht erst beim Aufräumen danach.
    if (frame.disposal == 3)
    {
        backup_ = CloneCanvas();
        if (!backup_)
        {
            error = L"Zwischenstand des GIFs konnte nicht gesichert werden.";
            return false;
        }
    }

    const UINT width = ClipWidth(frame);
    const UINT height = ClipHeight(frame);
    if (width == 0 || height == 0)
        return true;   // liegt außerhalb der Leinwand

    ComPtr<IWICBitmapSource> source = document_->LoadFrame(factory_.Get(), index, error);
    if (!source)
        return false;

    // Die Grenzen aus Load halten die Anforderung klein; scheitert sie auf
    // einem knappen Rechner trotzdem, wird daraus eine Meldung und kein
    // unbehandeltes bad_alloc, das das Programm über std::terminate risse.
    // new(nothrow) statt Behälter, wie beim Drucken: übersetzt wird ohne /EH,
    // ein catch hätte hier also keine geordnete Abwicklung hinter sich.
    const UINT stride = frame.width * kBytes;
    const size_t bytes = static_cast<size_t>(stride) * frame.height;
    std::unique_ptr<BYTE[]> pixels(new (std::nothrow) BYTE[bytes]);
    if (!pixels)
    {
        error = L"Für das Einzelbild des GIFs ist nicht genug Speicher frei.";
        return false;
    }
    if (FAILED(source->CopyPixels(nullptr, stride, static_cast<UINT>(bytes), pixels.get())))
    {
        error = L"Einzelbild des GIFs konnte nicht gelesen werden.";
        return false;
    }

    WICRect rect = { static_cast<INT>(frame.left), static_cast<INT>(frame.top),
                     static_cast<INT>(width), static_cast<INT>(height) };
    ComPtr<IWICBitmapLock> lock;
    if (FAILED(canvas_->Lock(&rect, WICBitmapLockRead | WICBitmapLockWrite, &lock)))
    {
        error = L"Leinwand des GIFs ist nicht beschreibbar.";
        return false;
    }

    UINT canvasStride = 0;
    UINT size = 0;
    BYTE* data = nullptr;
    if (FAILED(lock->GetStride(&canvasStride)) || FAILED(lock->GetDataPointer(&size, &data)))
    {
        error = L"Leinwand des GIFs ist nicht beschreibbar.";
        return false;
    }

    for (UINT y = 0; y < height; ++y)
    {
        BYTE* dst = data + static_cast<size_t>(y) * canvasStride;
        const BYTE* src = pixels.get() + static_cast<size_t>(y) * stride;
        for (UINT x = 0; x < width; ++x, dst += kBytes, src += kBytes)
        {
            const UINT alpha = src[3];
            if (alpha == 0)
                continue;                       // durchsichtig: es bleibt, was darunter liegt
            if (alpha == 255)
            {
                std::memcpy(dst, src, kBytes);
                continue;
            }

            // Vormultipliziert überlagern: dst = src + dst * (1 - a). GIF kennt
            // nur ganz oder gar nicht durchsichtig, aber der allgemeine Fall
            // kostet nichts und bleibt richtig, falls WIC doch einmal mischt.
            const UINT inverse = 255u - alpha;
            for (UINT c = 0; c < kBytes; ++c)
            {
                const UINT blended = src[c] + (dst[c] * inverse + 127u) / 255u;
                dst[c] = static_cast<BYTE>(std::min<UINT>(blended, 255u));
            }
        }
    }
    return true;
}

ComPtr<IWICBitmapSource> GifAnimator::Compose(UINT index, std::wstring& error)
{
    if (!IsActive() || index >= frames_.size())
    {
        error = L"Ungültiges Einzelbild.";
        return nullptr;
    }
    if (caching_ && cache_[index])
        return cache_[index];

    if (!EnsureCanvas())
    {
        error = L"Leinwand für das GIF konnte nicht angelegt werden.";
        return nullptr;
    }

    // Rückwärts: die Aufräumregeln bauen aufeinander auf, ein Stand lässt
    // sich nur vorwärts herstellen. Also von vorn nachspielen.
    if (composed_ > static_cast<int>(index))
    {
        if (!ClearArea(canvas_.Get(), 0, 0, canvasWidth_, canvasHeight_))
        {
            error = L"Leinwand des GIFs konnte nicht geleert werden.";
            return nullptr;
        }
        backup_.Reset();
        composed_ = -1;
    }

    while (composed_ < static_cast<int>(index))
    {
        if (composed_ >= 0 && !ApplyDisposal(static_cast<UINT>(composed_)))
        {
            error = L"Einzelbild des GIFs konnte nicht aufgeräumt werden.";
            return nullptr;
        }

        const UINT next = static_cast<UINT>(composed_ + 1);
        if (!DrawFrame(next, error))
            return nullptr;
        composed_ = static_cast<int>(next);

        if (caching_ && !cache_[next])
            cache_[next] = CloneCanvas();
    }

    if (caching_ && cache_[index])
        return cache_[index];

    // Ohne Zwischenspeicher wird die laufende Leinwand selbst herausgereicht.
    // Sie wandert sofort in eine Direct2D-Bitmap; erst danach schreibt der
    // nächste Frame wieder hinein.
    return canvas_;
}
