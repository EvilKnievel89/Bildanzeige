// Testdaten an den Grenzen. Schreibt in das laufende Verzeichnis:
//
//   riesig.jpg   8000 x 8000 -- gross genug, dass das Dekodieren messbar
//                dauert (256 MB Textur). Ein Schachbrett aus 500er-Feldern:
//                es laesst sich zaehlen und komprimiert trotzdem gut.
//   ueberbreit.png  20000 x 400 -- jenseits der Texturgrenze (16384). Muss
//                verkleinert hochgeladen werden, ohne dass der Massstab luegt.
//   kaputt.png   ein PNG, dem der Rumpf fehlt: auf ein Fuenftel gekuerzt.
//
// Zum letzten gehoert ein Befund: WIC zeigt es trotzdem an. Gesucht war eine
// Datei, an der der Fehlerweg des Hintergrund-Threads anschlaegt -- weder
// Kuerzen noch verfaelschte Bytes reichen dafuer. Was wirklich anschlaegt,
// ist ein Pfad, der verschwindet (PLAN.md, Abschnitt 7).

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

    bool WritePng(const wchar_t* path, UINT width, UINT height,
                  const std::vector<BYTE>& pixels, const GUID& container)
    {
        ComPtr<IWICStream> stream;
        if (FAILED(g_factory->CreateStream(&stream))) return false;
        if (FAILED(stream->InitializeFromFilename(path, GENERIC_WRITE))) return false;

        ComPtr<IWICBitmapEncoder> encoder;
        if (FAILED(g_factory->CreateEncoder(container, nullptr, &encoder))) return false;
        if (FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return false;

        ComPtr<IWICBitmapFrameEncode> frame;
        if (FAILED(encoder->CreateNewFrame(&frame, nullptr))) return false;
        if (FAILED(frame->Initialize(nullptr))) return false;
        if (FAILED(frame->SetSize(width, height))) return false;

        WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
        if (FAILED(frame->SetPixelFormat(&format))) return false;

        const UINT stride = width * 3;
        if (FAILED(frame->WritePixels(height, stride, static_cast<UINT>(pixels.size()),
                                      const_cast<BYTE*>(pixels.data()))))
            return false;

        if (FAILED(frame->Commit())) return false;
        return SUCCEEDED(encoder->Commit());
    }

    // Schachbrett: jedes Feld ist entweder weiss oder dunkelgrau. Ueber die
    // Zahl der weissen Bildpunkte laesst sich spaeter nachrechnen, dass wirklich
    // das ganze Bild angekommen ist.
    std::vector<BYTE> Checkerboard(UINT width, UINT height, UINT field)
    {
        std::vector<BYTE> pixels(static_cast<size_t>(width) * height * 3, 0);
        for (UINT y = 0; y < height; ++y)
        {
            BYTE* row = pixels.data() + static_cast<size_t>(y) * width * 3;
            for (UINT x = 0; x < width; ++x)
            {
                const bool light = (((x / field) + (y / field)) % 2) == 0;
                const BYTE value = light ? 255 : 32;
                row[x * 3 + 0] = value;
                row[x * 3 + 1] = value;
                row[x * 3 + 2] = value;
            }
        }
        return pixels;
    }

    // Ein weisser Block in der linken oberen Ecke, sonst dunkel -- wie schon bei
    // den Drehbildern. Damit ist die Lage nach dem Verkleinern noch ablesbar.
    std::vector<BYTE> CornerBlock(UINT width, UINT height, UINT block)
    {
        std::vector<BYTE> pixels(static_cast<size_t>(width) * height * 3, 24);
        for (UINT y = 0; y < block && y < height; ++y)
        {
            BYTE* row = pixels.data() + static_cast<size_t>(y) * width * 3;
            for (UINT x = 0; x < block && x < width; ++x)
            {
                row[x * 3 + 0] = 255;
                row[x * 3 + 1] = 255;
                row[x * 3 + 2] = 255;
            }
        }
        return pixels;
    }

    // Ein gueltiges PNG erzeugen und danach hinten abschneiden: IHDR mit
    // Groesse bleibt stehen, der Datenstrom bricht mittendrin ab.
    //
    // Erst als JPEG versucht -- das verzeiht WIC aber: es meldet zwar "premature
    // end of data segment", liefert die Pixel trotzdem und faellt damit als
    // Fehlerfall aus. PNG traegt Pruefsummen je Block und bricht wirklich ab.
    bool WriteTruncated(const wchar_t* path, UINT width, UINT height)
    {
        const std::wstring temp = std::wstring(path) + L".voll";
        if (!WritePng(temp.c_str(), width, height, Checkerboard(width, height, 3),
                      GUID_ContainerFormatPng))
            return false;

        HANDLE file = CreateFileW(temp.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;

        DWORD size = GetFileSize(file, nullptr);
        std::vector<BYTE> data(size);
        DWORD read = 0;
        ReadFile(file, data.data(), size, &read, nullptr);
        CloseHandle(file);
        DeleteFileW(temp.c_str());

        // Ein Fuenftel behalten: Kopf und Tabellen sind durch, die Bilddaten
        // brechen mittendrin ab.
        const DWORD keep = read / 5;
        HANDLE out = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
        if (out == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        WriteFile(out, data.data(), keep, &written, nullptr);
        CloseHandle(out);

        wprintf(L"  kaputt.png: von %lu auf %lu Bytes gekuerzt\n", read, keep);
        return written == keep;
    }

    void Report(const wchar_t* path)
    {
        ComPtr<IWICBitmapDecoder> decoder;
        HRESULT hr = g_factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
                                                          WICDecodeMetadataCacheOnDemand, &decoder);
        if (FAILED(hr))
        {
            wprintf(L"  %-16s oeffnen fehlgeschlagen (0x%08lX)\n", path, hr);
            return;
        }

        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, &frame)))
        {
            wprintf(L"  %-16s Frame 0 nicht lesbar\n", path);
            return;
        }

        UINT w = 0, h = 0;
        frame->GetSize(&w, &h);

        // Probeweise dekodieren -- genau das tut spaeter der Hintergrund-Thread.
        ComPtr<IWICFormatConverter> converter;
        g_factory->CreateFormatConverter(&converter);
        hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom);
        ComPtr<IWICBitmap> bitmap;
        if (SUCCEEDED(hr))
        {
            const LONGLONG start = GetTickCount64();
            hr = g_factory->CreateBitmapFromSource(converter.Get(), WICBitmapCacheOnLoad, &bitmap);
            const LONGLONG spent = GetTickCount64() - start;
            wprintf(L"  %-16s %5u x %-5u  dekodieren: %s (%lld ms)\n", path, w, h,
                    SUCCEEDED(hr) ? L"ok    " : L"FEHLER", spent);
            return;
        }
        wprintf(L"  %-16s %5u x %-5u  dekodieren: FEHLER\n", path, w, h);
    }
}

int wmain()
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&g_factory))))
    {
        wprintf(L"WIC nicht verfuegbar\n");
        return 1;
    }

    wprintf(L"Erzeugen:\n");
    // Als JPEG, nicht als PNG: ein PNG-Schachbrett ist in 78 ms entpackt und
    // wuerde ueberhaupt nichts messbar machen. JPEG rechnet je Bloeckchen und
    // braucht bei 64 Megapixeln spuerbar laenger.
    if (!WritePng(L"riesig.jpg", 8000, 8000, Checkerboard(8000, 8000, 500),
                  GUID_ContainerFormatJpeg))
        wprintf(L"  riesig.jpg FEHLGESCHLAGEN\n");

    if (!WritePng(L"ueberbreit.png", 20000, 400, CornerBlock(20000, 400, 200),
                  GUID_ContainerFormatPng))
        wprintf(L"  ueberbreit.png FEHLGESCHLAGEN\n");

    if (!WriteTruncated(L"kaputt.png", 800, 600))
        wprintf(L"  kaputt.png FEHLGESCHLAGEN\n");

    wprintf(L"\nZurueckgelesen:\n");
    Report(L"riesig.jpg");
    Report(L"ueberbreit.png");
    Report(L"kaputt.png");

    g_factory.Reset();
    CoUninitialize();
    return 0;
}
