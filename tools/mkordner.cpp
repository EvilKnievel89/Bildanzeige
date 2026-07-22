// Testdaten fuer Ordnerliste und EXIF-Orientierung. Schreibt in das laufende
// Verzeichnis und liest alles zur Kontrolle zurueck:
//
//   ordner\Bild1.png, Bild2.png, Bild10.png   -- 1, 2 bzw. 10 weisse Quadrate
//   dreh<N>.jpg                               -- EXIF-Orientierung N (1..8)
//
// Von den acht Drehungen liegen nur dreh1 und dreh6 in testdata: die eine als
// Vergleichsstueck, die andere als der Fall, der hochkant erscheinen muss.
// Die uebrigen sechs entstehen mit und sind zum Nachsehen da.
//
// Die Quadratzahl macht im laufenden Programm zaehlbar, welche Datei gerade
// zu sehen ist; die Namen zeigen, ob natuerlich sortiert wird (1, 2, 10) oder
// lexikografisch (1, 10, 2).

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdio>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
    ComPtr<IWICImagingFactory> g_factory;

    // 32bppBGRA-Puffer, damit sich bequem hineinschreiben laesst.
    struct Canvas
    {
        UINT width = 0;
        UINT height = 0;
        std::vector<BYTE> pixels;

        void Create(UINT w, UINT h, BYTE grey)
        {
            width = w;
            height = h;
            pixels.assign(static_cast<size_t>(w) * h * 4, 0);
            for (size_t i = 0; i < pixels.size(); i += 4)
            {
                pixels[i + 0] = grey;
                pixels[i + 1] = grey;
                pixels[i + 2] = grey;
                pixels[i + 3] = 255;
            }
        }

        void Fill(UINT left, UINT top, UINT w, UINT h, BYTE b, BYTE g, BYTE r)
        {
            for (UINT y = top; y < top + h && y < height; ++y)
            {
                for (UINT x = left; x < left + w && x < width; ++x)
                {
                    BYTE* p = &pixels[(static_cast<size_t>(y) * width + x) * 4];
                    p[0] = b;
                    p[1] = g;
                    p[2] = r;
                    p[3] = 255;
                }
            }
        }
    };

    HRESULT Save(const Canvas& canvas, const wchar_t* path, REFGUID container,
                 REFGUID pixelFormat, unsigned orientation)
    {
        ComPtr<IWICBitmap> bitmap;
        HRESULT hr = g_factory->CreateBitmapFromMemory(
            canvas.width, canvas.height, GUID_WICPixelFormat32bppBGRA, canvas.width * 4,
            static_cast<UINT>(canvas.pixels.size()),
            const_cast<BYTE*>(canvas.pixels.data()), &bitmap);
        if (FAILED(hr))
            return hr;

        ComPtr<IWICBitmapSource> source;
        hr = WICConvertBitmapSource(pixelFormat, bitmap.Get(), &source);
        if (FAILED(hr))
            return hr;

        ComPtr<IWICStream> stream;
        hr = g_factory->CreateStream(&stream);
        if (FAILED(hr))
            return hr;
        hr = stream->InitializeFromFilename(path, GENERIC_WRITE);
        if (FAILED(hr))
            return hr;

        ComPtr<IWICBitmapEncoder> encoder;
        hr = g_factory->CreateEncoder(container, nullptr, &encoder);
        if (FAILED(hr))
            return hr;
        hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
        if (FAILED(hr))
            return hr;

        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2> options;
        hr = encoder->CreateNewFrame(&frame, &options);
        if (FAILED(hr))
            return hr;
        hr = frame->Initialize(options.Get());
        if (FAILED(hr))
            return hr;

        if (orientation != 0)
        {
            ComPtr<IWICMetadataQueryWriter> writer;
            hr = frame->GetMetadataQueryWriter(&writer);
            if (SUCCEEDED(hr))
            {
                PROPVARIANT value;
                PropVariantInit(&value);
                value.vt = VT_UI2;
                value.uiVal = static_cast<USHORT>(orientation);
                hr = writer->SetMetadataByName(L"/app1/ifd/{ushort=274}", &value);
                PropVariantClear(&value);
            }
            if (FAILED(hr))
                wprintf(L"    SetMetadataByName -> 0x%08X\n", hr);
        }

        hr = frame->WriteSource(source.Get(), nullptr);
        if (FAILED(hr))
            return hr;
        hr = frame->Commit();
        if (FAILED(hr))
            return hr;
        return encoder->Commit();
    }

    // Liest genau so zurueck, wie ImageDocument::ReadOrientation es tut.
    void Report(const wchar_t* path)
    {
        ComPtr<IWICBitmapDecoder> decoder;
        HRESULT hr = g_factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
                                                          WICDecodeMetadataCacheOnDemand, &decoder);
        if (FAILED(hr))
        {
            wprintf(L"  %-24s OEFFNEN FEHLGESCHLAGEN 0x%08X\n", path, hr);
            return;
        }

        ComPtr<IWICBitmapFrameDecode> frame;
        decoder->GetFrame(0, &frame);
        UINT w = 0, h = 0;
        if (frame)
            frame->GetSize(&w, &h);

        unsigned orientation = 0;
        ComPtr<IWICMetadataQueryReader> reader;
        if (frame && SUCCEEDED(frame->GetMetadataQueryReader(&reader)))
        {
            for (const wchar_t* query : { L"/app1/ifd/{ushort=274}", L"/ifd/{ushort=274}" })
            {
                PROPVARIANT value;
                PropVariantInit(&value);
                if (SUCCEEDED(reader->GetMetadataByName(query, &value)) && value.vt == VT_UI2)
                    orientation = value.uiVal;
                PropVariantClear(&value);
                if (orientation != 0)
                    break;
            }
        }

        wprintf(L"  %-24s %4u x %-4u  Orientierung=%u\n", path, w, h, orientation);
    }
}

int wmain()
{
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
        return 1;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&g_factory))))
        return 1;

    CreateDirectoryW(L"ordner", nullptr);

    // ---- Ordnerdateien: N Quadrate zu 32x32, fuenf je Reihe --------------
    struct { const wchar_t* name; UINT squares; } folder[] = {
        { L"ordner\\Bild1.png", 1 }, { L"ordner\\Bild2.png", 2 }, { L"ordner\\Bild10.png", 10 },
    };

    for (const auto& entry : folder)
    {
        Canvas canvas;
        canvas.Create(240, 160, 0x30);
        for (UINT i = 0; i < entry.squares; ++i)
            canvas.Fill(24 + (i % 5) * 40, 32 + (i / 5) * 64, 32, 32, 255, 255, 255);

        const HRESULT hr = Save(canvas, entry.name, GUID_ContainerFormatPng,
                                GUID_WICPixelFormat32bppBGRA, 0);
        wprintf(L"%-24s %2u Quadrate -> 0x%08X\n", entry.name, entry.squares, hr);
    }

    // ---- EXIF-Orientierungen 1..8 ----------------------------------------
    // 1200x300 quer, weisser Block in der gespeicherten linken oberen Ecke.
    // Nach der Drehung muss er in genau einer Ecke stehen, und bei 90/270 Grad
    // wird aus dem Querformat ein Hochformat.
    for (unsigned orientation = 1; orientation <= 8; ++orientation)
    {
        Canvas canvas;
        canvas.Create(1200, 300, 0x30);
        canvas.Fill(0, 0, 120, 120, 255, 255, 255);
        // Ein schmaler Streifen laengs, damit auch ohne Zaehlen zu sehen ist,
        // wo oben ist.
        canvas.Fill(0, 140, 1200, 12, 0x90, 0x90, 0x90);

        wchar_t path[64];
        swprintf_s(path, L"dreh%u.jpg", orientation);
        const HRESULT hr = Save(canvas, path, GUID_ContainerFormatJpeg,
                                GUID_WICPixelFormat24bppBGR, orientation);
        wprintf(L"%-24s Orientierung %u -> 0x%08X\n", path, orientation, hr);
    }

    wprintf(L"\nZurueckgelesen:\n");
    for (const auto& entry : folder)
        Report(entry.name);
    for (unsigned orientation = 1; orientation <= 8; ++orientation)
    {
        wchar_t path[64];
        swprintf_s(path, L"dreh%u.jpg", orientation);
        Report(path);
    }

    g_factory.Reset();
    CoUninitialize();
    return 0;
}
