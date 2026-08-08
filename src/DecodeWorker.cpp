// Bildanzeige -- Copyright (C) 2026 EvilKnievel89
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Freie Software unter der GNU GPL, Version 3 oder einer späteren: weitergeben
// und verändern ist erlaubt, solange Quelltext und Lizenz mitgehen. Ohne jede
// Gewährleistung. Wortlaut in LICENSE, Erklärung in LIZENZ.md.

#include "DecodeWorker.h"

#include <algorithm>

namespace
{
    // Ein vollständiger Auftrag: Datei öffnen, Seite lesen, in das Format
    // bringen, das Direct2D annimmt, und die Pixel in den Speicher holen.
    //
    // Der Decoder wird hier erzeugt und hier wieder verworfen. Ihn stattdessen
    // an den UI-Thread zu reichen wäre naheliegend -- er ist ja schon offen --
    // hätten dann aber zwei Apartments dasselbe Objekt in der Hand. Ein
    // erneutes Öffnen kostet nur das Lesen des Dateikopfes; das Dekodieren der
    // Pixel, um das es hier geht, fällt so oder so nur einmal an.
    void Decode(IWICImagingFactory* factory, const std::wstring& path, UINT frameIndex,
                UINT maxTexture, DecodeResult& result)
    {
        if (factory == nullptr)
        {
            result.error = L"WIC ist auf dem Hintergrund-Thread nicht verfügbar.";
            return;
        }

        ComPtr<IWICBitmapDecoder> decoder;
        HRESULT hr = factory->CreateDecoderFromFilename(
            path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
        if (FAILED(hr))
        {
            result.error = L"Datei konnte nicht geöffnet werden.\n\n" + FormatHResult(hr);
            return;
        }

        ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(frameIndex, &frame);
        if (FAILED(hr))
        {
            result.error = L"Frame konnte nicht gelesen werden.\n\n" + FormatHResult(hr);
            return;
        }

        hr = frame->GetSize(&result.sourceWidth, &result.sourceHeight);
        if (FAILED(hr) || result.sourceWidth == 0 || result.sourceHeight == 0)
        {
            result.error = L"Bildgröße konnte nicht bestimmt werden.\n\n" + FormatHResult(hr);
            return;
        }

        // Vor jeder weiteren Arbeit, denn ab hier kosten die behaupteten Maße
        // Speicher. Warum es die Grenze gibt, steht bei kMaxImagePixels.
        if (static_cast<unsigned long long>(result.sourceWidth) * result.sourceHeight >
            kMaxImagePixels)
        {
            result.error = L"Das Bild meldet " + std::to_wstring(result.sourceWidth) + L" x " +
                           std::to_wstring(result.sourceHeight) +
                           L" Bildpunkte. Mehr als 250 Megapixel zeigt die Anzeige nicht an.";
            return;
        }

        // Ohne die Umwandlung nach 32bppPBGRA bleiben etwa 1-bpp-Fax-TIFFs
        // unsichtbar -- Direct2D nimmt ihr Format nicht an.
        ComPtr<IWICFormatConverter> converter;
        hr = factory->CreateFormatConverter(&converter);
        if (SUCCEEDED(hr))
        {
            hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                                       WICBitmapDitherTypeNone, nullptr, 0.0,
                                       WICBitmapPaletteTypeCustom);
        }
        if (FAILED(hr))
        {
            result.error = L"Bildformat wird nicht unterstützt.\n\n" + FormatHResult(hr);
            return;
        }

        ComPtr<IWICBitmapSource> source;
        hr = converter.As(&source);
        if (FAILED(hr))
        {
            result.error = FormatHResult(hr);
            return;
        }

        // Großformatige Scans sprengen die Texturgrenze der GPU (je nach
        // Gerät 8k-16k). Verkleinert wird hier, nicht später im UI-Thread:
        // gerade bei einem solchen Bild ist das Herunterrechnen selbst die
        // Arbeit, die das Fenster stehenlassen würde.
        if (maxTexture > 0 && (result.sourceWidth > maxTexture || result.sourceHeight > maxTexture))
        {
            const double factor = static_cast<double>(maxTexture) /
                                  static_cast<double>(std::max(result.sourceWidth,
                                                               result.sourceHeight));
            const UINT width = std::max<UINT>(1, static_cast<UINT>(result.sourceWidth * factor));
            const UINT height = std::max<UINT>(1, static_cast<UINT>(result.sourceHeight * factor));

            ComPtr<IWICBitmapScaler> scaler;
            hr = factory->CreateBitmapScaler(&scaler);
            if (SUCCEEDED(hr))
                hr = scaler->Initialize(source.Get(), width, height,
                                        WICBitmapInterpolationModeFant);
            if (FAILED(hr))
            {
                result.error = L"Bild konnte nicht auf die Texturgrenze gebracht werden.\n\n" +
                               FormatHResult(hr);
                return;
            }
            source.Reset();
            hr = scaler.As(&source);
            if (FAILED(hr))
            {
                result.error = FormatHResult(hr);
                return;
            }
        }

        // Hier fällt die eigentliche Arbeit an: WICBitmapCacheOnLoad zieht die
        // Pixel sofort in den Speicher, statt sie erst beim Auslesen zu
        // erzeugen. Genau dafür gibt es diesen Thread -- geschähe es erst im
        // UI-Thread beim Hochladen zur GPU, wäre nichts gewonnen.
        ComPtr<IWICBitmap> bitmap;
        hr = factory->CreateBitmapFromSource(source.Get(), WICBitmapCacheOnLoad, &bitmap);
        if (FAILED(hr))
        {
            result.error = L"Bild konnte nicht dekodiert werden.\n\n" + FormatHResult(hr);
            return;
        }

        result.bitmap = bitmap;
    }
}

DecodeWorker::~DecodeWorker()
{
    Stop();
}

bool DecodeWorker::Start(HWND notify, UINT message)
{
    if (thread_.joinable())
        return true;

    notify_ = notify;
    message_ = message;
    quit_ = false;
    thread_ = std::thread(&DecodeWorker::Run, this);
    return true;
}

void DecodeWorker::Stop()
{
    if (!thread_.joinable())
        return;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        quit_ = true;
    }
    signal_.notify_one();

    // Ein laufender Auftrag lässt sich nicht abbrechen -- WIC kennt keinen
    // Weg, mitten aus dem Dekodieren herauszukommen. Beim Beenden wartet das
    // Programm deshalb höchstens ein Bild lang; das Fenster ist da bereits fort.
    thread_.join();
}

unsigned long long DecodeWorker::Request(const std::wstring& path, UINT frameIndex,
                                         UINT maxTexture)
{
    if (!thread_.joinable())
        return 0;

    unsigned long long token = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        path_ = path;
        frameIndex_ = frameIndex;
        maxTexture_ = maxTexture;
        token = ++token_;
        hasRequest_ = true;
    }
    signal_.notify_one();
    return token;
}

bool DecodeWorker::Take(DecodeResult& out)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!hasResult_)
        return false;

    out = std::move(result_);
    result_ = DecodeResult{};
    hasResult_ = false;
    return true;
}

void DecodeWorker::Run()
{
    // Eigenes Apartment, und zwar MTA: hier hängt nichts an einem Fenster, und
    // eine Nachrichtenschleife, wie ein STA sie bräuchte, läuft nicht.
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // Eine eigene Fabrik statt der des UI-Threads -- sie kostet nichts und
    // erspart die Frage, ob zwei Threads sich eine teilen dürfen.
    ComPtr<IWICImagingFactory> factory;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                     IID_PPV_ARGS(&factory));

    for (;;)
    {
        std::wstring path;
        UINT frameIndex = 0;
        UINT maxTexture = 0;
        unsigned long long token = 0;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            signal_.wait(lock, [this] { return quit_ || hasRequest_; });
            if (quit_)
                break;

            path = path_;
            frameIndex = frameIndex_;
            maxTexture = maxTexture_;
            token = token_;
            hasRequest_ = false;
        }

        DecodeResult result;
        result.token = token;
        result.frameIndex = frameIndex;
        Decode(factory.Get(), path, frameIndex, maxTexture, result);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (quit_)
                break;

            // Während der Arbeit ist bereits der nächste Auftrag eingegangen:
            // dieses Ergebnis ist überholt und wird gar nicht erst gemeldet.
            if (hasRequest_)
                continue;

            result_ = std::move(result);
            hasResult_ = true;
        }
        PostMessageW(notify_, message_, 0, 0);
    }

    factory.Reset();
    if (SUCCEEDED(init))
        CoUninitialize();
}
