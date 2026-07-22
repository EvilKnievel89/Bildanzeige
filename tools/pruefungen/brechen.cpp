// Sucht eine Beschaedigung, die WIC wirklich nicht mehr verzeiht.
//
//     brechen.exe ..\..\testdata\gross.png ..\..\testdata\dreh1.jpg
//
// Abschneiden genuegt nicht: das JPEG meldet "premature end of data segment"
// und liefert die Pixel trotzdem, das PNG ebenso. Hier werden stattdessen
// Bytes mitten im Datenstrom verfaelscht -- bei PNG bricht dann die
// Pruefsumme des Blocks, bei JPEG der Huffman-Strom.
//
// Das Ergebnis ist die Pointe: auch das reicht nicht. Alle sechs Faelle kommen
// ohne Fehler durch. Wer im Hintergrund-Thread auf WINCODEC_ERR_BADIMAGE
// wartet, wartet lange -- was den Fehlerweg wirklich ausloest, ist ein Pfad,
// der verschwindet (PLAN.md, Abschnitt 7).

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

    std::vector<BYTE> Read(const wchar_t* path)
    {
        HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return {};
        const DWORD size = GetFileSize(file, nullptr);
        std::vector<BYTE> data(size);
        DWORD read = 0;
        ReadFile(file, data.data(), size, &read, nullptr);
        CloseHandle(file);
        data.resize(read);
        return data;
    }

    bool Write(const wchar_t* path, const std::vector<BYTE>& data)
    {
        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
        CloseHandle(file);
        return written == data.size();
    }

    // Genau der Ablauf des Hintergrund-Threads: oeffnen, Frame holen, umwandeln,
    // Pixel in den Speicher ziehen.
    const wchar_t* Try(const wchar_t* path, HRESULT& where)
    {
        ComPtr<IWICBitmapDecoder> decoder;
        where = g_factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
                                                     WICDecodeMetadataCacheOnDemand, &decoder);
        if (FAILED(where)) return L"oeffnen";

        ComPtr<IWICBitmapFrameDecode> frame;
        where = decoder->GetFrame(0, &frame);
        if (FAILED(where)) return L"GetFrame";

        UINT w = 0, h = 0;
        where = frame->GetSize(&w, &h);
        if (FAILED(where)) return L"GetSize";

        ComPtr<IWICFormatConverter> converter;
        where = g_factory->CreateFormatConverter(&converter);
        if (SUCCEEDED(where))
            where = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                                          WICBitmapDitherTypeNone, nullptr, 0.0,
                                          WICBitmapPaletteTypeCustom);
        if (FAILED(where)) return L"Initialize";

        ComPtr<IWICBitmap> bitmap;
        where = g_factory->CreateBitmapFromSource(converter.Get(), WICBitmapCacheOnLoad, &bitmap);
        if (FAILED(where)) return L"dekodieren";
        return nullptr;
    }

    void Probe(const wchar_t* source, const wchar_t* target, double at, const wchar_t* label)
    {
        std::vector<BYTE> data = Read(source);
        if (data.empty()) { wprintf(L"  %s: Quelle fehlt\n", label); return; }

        const size_t start = static_cast<size_t>(data.size() * at);
        for (size_t i = start; i < data.size() && i < start + 64; ++i)
            data[i] = static_cast<BYTE>(~data[i]);

        if (!Write(target, data)) { wprintf(L"  %s: schreiben fehlgeschlagen\n", label); return; }

        HRESULT where = S_OK;
        const wchar_t* stage = Try(target, where);
        if (stage == nullptr)
            wprintf(L"  %-28s -> durchgelassen (kein Fehler)\n", label);
        else
            wprintf(L"  %-28s -> scheitert bei %s (0x%08lX)\n", label, stage, where);
    }
}

int wmain(int argc, wchar_t** argv)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&g_factory))))
        return 1;

    const wchar_t* png = argc > 1 ? argv[1] : L"gross.png";
    const wchar_t* jpg = argc > 2 ? argv[2] : L"dreh1.jpg";

    wprintf(L"64 Bytes invertiert, an verschiedenen Stellen:\n");
    for (double at : { 0.20, 0.50, 0.80 })
    {
        wchar_t label[64];
        swprintf(label, 64, L"PNG bei %.0f %%", at * 100);
        Probe(png, L"probe.png", at, label);
    }
    for (double at : { 0.20, 0.50, 0.80 })
    {
        wchar_t label[64];
        swprintf(label, 64, L"JPEG bei %.0f %%", at * 100);
        Probe(jpg, L"probe.jpg", at, label);
    }

    DeleteFileW(L"probe.png");
    DeleteFileW(L"probe.jpg");
    g_factory.Reset();
    CoUninitialize();
    return 0;
}
