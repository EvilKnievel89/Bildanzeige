// langsam.png -- 60000 x 2000, also 120 Megapixel.
//
// Gesucht ist eine Datei, deren Dekodierung ueber 150 ms braucht: erst dann
// wird der Ladezustand ueberhaupt sichtbar. Ein gleich grosses Quadrat waere
// dafuer untauglich, weil es einen halben Gigabyte Zielpuffer braeuchte.
//
// Diese Form ist umgekehrt gebaut: viele Bildpunkte am Eingang, aber nur 16384
// Punkte Breite als Textur. WIC zieht die Zeilen durch den Verkleinerer, ohne
// je das Ganze im Speicher zu halten -- viel Arbeit, wenig Speicher.
//
// Erst als CCITT-G4-TIFF versucht: der WIC-Encoder kam bei dieser Zeilenbreite
// nach zehn Minuten nicht ueber den Dateikopf hinaus. PNG schreibt dieselbe
// Form in Sekunden.

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdio>
#include <vector>

using Microsoft::WRL::ComPtr;

int wmain()
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))))
        return 1;

    const UINT width = 60000;
    const UINT height = 2000;
    const UINT stride = width * 3;
    const UINT chunk = 50;   // Zeilen je Schreibvorgang -- sonst 360 MB am Stueck

    ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(&stream))) return 1;
    if (FAILED(stream->InitializeFromFilename(L"langsam.png", GENERIC_WRITE))) return 1;

    ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))) return 1;
    if (FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return 1;

    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(encoder->CreateNewFrame(&frame, nullptr))) return 1;
    if (FAILED(frame->Initialize(nullptr))) return 1;
    if (FAILED(frame->SetSize(width, height))) return 1;

    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    if (FAILED(frame->SetPixelFormat(&format))) return 1;

    // Ein weisser Block in der linken oberen Ecke, sonst senkrechte Streifen
    // mit wechselnder Teilung. Der Block sagt hinterher, wie herum das Bild
    // haengt; die Streifen sorgen dafuer, dass beim Verkleinern wirklich
    // gerechnet werden muss.
    std::vector<BYTE> rows(static_cast<size_t>(stride) * chunk);
    for (UINT y0 = 0; y0 < height; y0 += chunk)
    {
        const UINT rowsHere = (y0 + chunk <= height) ? chunk : (height - y0);
        for (UINT r = 0; r < rowsHere; ++r)
        {
            const UINT y = y0 + r;
            const UINT period = 3 + (y % 11);
            BYTE* dst = rows.data() + static_cast<size_t>(r) * stride;
            for (UINT x = 0; x < width; ++x)
            {
                BYTE value;
                if (y < 400 && x < 400)
                    value = 255;
                else
                    value = (((x / period) % 2) == 0) ? 210 : 30;
                dst[x * 3 + 0] = value;
                dst[x * 3 + 1] = value;
                dst[x * 3 + 2] = value;
            }
        }
        if (FAILED(frame->WritePixels(rowsHere, stride,
                                      static_cast<UINT>(stride) * rowsHere, rows.data())))
        {
            wprintf(L"WritePixels fehlgeschlagen bei Zeile %u\n", y0);
            return 1;
        }
    }

    if (FAILED(frame->Commit())) return 1;
    if (FAILED(encoder->Commit())) return 1;
    frame.Reset(); encoder.Reset(); stream.Reset();

    // Zurueckgelesen, und zwar genau wie der Hintergrund-Thread es tut.
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(L"langsam.png", nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnDemand, &decoder)))
        return 1;

    ComPtr<IWICBitmapFrameDecode> back;
    decoder->GetFrame(0, &back);
    UINT w = 0, h = 0;
    back->GetSize(&w, &h);

    ComPtr<IWICFormatConverter> converter;
    factory->CreateFormatConverter(&converter);
    converter->Initialize(back.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                          nullptr, 0.0, WICBitmapPaletteTypeCustom);

    const UINT maxTexture = 16384;
    ComPtr<IWICBitmapSource> source;
    converter.As(&source);
    UINT outW = w, outH = h;
    if (w > maxTexture)
    {
        const double f = static_cast<double>(maxTexture) / w;
        outW = maxTexture;
        outH = static_cast<UINT>(h * f);
        ComPtr<IWICBitmapScaler> scaler;
        factory->CreateBitmapScaler(&scaler);
        scaler->Initialize(source.Get(), outW, outH, WICBitmapInterpolationModeFant);
        source.Reset();
        scaler.As(&source);
    }

    const ULONGLONG start = GetTickCount64();
    ComPtr<IWICBitmap> bitmap;
    const HRESULT hr = factory->CreateBitmapFromSource(source.Get(), WICBitmapCacheOnLoad, &bitmap);
    const ULONGLONG spent = GetTickCount64() - start;

    wprintf(L"langsam.png  %u x %u -> Textur %u x %u\n", w, h, outW, outH);
    wprintf(L"  dekodieren + verkleinern: %s, %llu ms\n", SUCCEEDED(hr) ? L"ok" : L"FEHLER", spent);
    wprintf(L"  Zielpuffer: %.1f MB\n", outW * static_cast<double>(outH) * 4 / (1024.0 * 1024.0));

    bitmap.Reset(); source.Reset(); converter.Reset(); back.Reset(); decoder.Reset();
    factory.Reset();
    CoUninitialize();
    return 0;
}
