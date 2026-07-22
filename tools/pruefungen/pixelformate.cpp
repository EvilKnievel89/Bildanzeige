// In welchem Pixelformat kommen die Bilder herein?
//
//     pixelformate.exe ..\..\testdata\fax.tif ..\..\testdata\mehrseitig.tif
//
// Nennt je Seite Größe und Pixelformat und versucht dann genau den Schritt,
// den auch die Anwendung geht: die Wandlung nach 32bppPBGRA. Daraus stammt der
// Befund, dass ein Fax-TIFF mit 1 bpp hereinkommt und ohne diese Wandlung
// unsichtbar bliebe -- PLAN.md, Abschnitt 3.

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstdio>
using Microsoft::WRL::ComPtr;

static const wchar_t* PfName(const GUID& g) {
    if (g == GUID_WICPixelFormatBlackWhite)   return L"BlackWhite (1 bpp)";
    if (g == GUID_WICPixelFormat8bppIndexed)  return L"8bppIndexed";
    if (g == GUID_WICPixelFormat24bppBGR)     return L"24bppBGR";
    if (g == GUID_WICPixelFormat32bppBGR)     return L"32bppBGR";
    if (g == GUID_WICPixelFormat32bppBGRA)    return L"32bppBGRA";
    if (g == GUID_WICPixelFormat32bppPBGRA)   return L"32bppPBGRA";
    return L"(anderes)";
}

int wmain(int argc, wchar_t** argv) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ComPtr<IWICImagingFactory> f;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));

    for (int a = 1; a < argc; a++) {
        wprintf(L"\n%s\n", argv[a]);
        ComPtr<IWICBitmapDecoder> dec;
        HRESULT hr = f->CreateDecoderFromFilename(argv[a], nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnDemand, &dec);
        if (FAILED(hr)) { wprintf(L"  öffnen fehlgeschlagen 0x%08lX\n", (unsigned long)hr); continue; }

        UINT n = 0; dec->GetFrameCount(&n);
        wprintf(L"  Frames: %u\n", n);
        for (UINT i = 0; i < n; i++) {
            ComPtr<IWICBitmapFrameDecode> fr;
            if (FAILED(dec->GetFrame(i, &fr))) continue;
            UINT w = 0, h = 0; fr->GetSize(&w, &h);
            WICPixelFormatGUID pf{}; fr->GetPixelFormat(&pf);
            wprintf(L"    [%u] %5u x %-5u  %s", i, w, h, PfName(pf));

            // genau der Pfad, den die Anwendung geht
            ComPtr<IWICFormatConverter> conv;
            f->CreateFormatConverter(&conv);
            HRESULT ch = conv->Initialize(fr.Get(), GUID_WICPixelFormat32bppPBGRA,
                                          WICBitmapDitherTypeNone, nullptr, 0.0,
                                          WICBitmapPaletteTypeCustom);
            wprintf(L"   -> 32bppPBGRA: %s\n", SUCCEEDED(ch) ? L"OK" : L"FEHLER");
        }
    }
    return 0;
}
