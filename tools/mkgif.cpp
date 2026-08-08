// Bildanzeige -- Copyright (C) 2026 EvilKnievel89
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Freie Software unter der GNU GPL, Version 3 oder einer späteren: weitergeben
// und verändern ist erlaubt, solange Quelltext und Lizenz mitgehen. Ohne jede
// Gewährleistung. Wortlaut in LICENSE, Erklärung in LIZENZ.md.

// Erzeugt Test-GIFs, bei denen ein Fehler in der Komposition sofort auffällt,
// und liest sie zur Kontrolle zurück. Schreibt in das laufende Verzeichnis:
//
//   anim.gif      3 Teilrechtecke, Verzögerungen 50/25/100
//   spur.gif      nach Einzelbild N müssen genau N Quadrate stehen
//   disposal.gif  Aufräumregeln 1, 2 und 3 nacheinander
//   zweimal.gif   Wiederholungszähler 2 -- muss danach stehenbleiben
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstdio>
#include <vector>
using Microsoft::WRL::ComPtr;

#define CK(x) do{ HRESULT _h=(x); if(FAILED(_h)){ wprintf(L"FAIL %S -> 0x%08lX\n",#x,(unsigned long)_h); return false; } }while(0)

struct Frame {
    UINT w, h, left, top;
    USHORT delay;
    BYTE disposal;
    std::vector<BYTE> px;      // 8bpp-Indizes
};

static void Fill(Frame& f, UINT x0, UINT y0, UINT w, UINT h, BYTE idx) {
    for (UINT y = y0; y < y0 + h && y < f.h; y++)
        for (UINT x = x0; x < x0 + w && x < f.w; x++)
            f.px[(size_t)y * f.w + x] = idx;
}

static Frame Make(UINT w, UINT h, UINT left, UINT top, USHORT delay, BYTE disposal, BYTE bg) {
    Frame f{ w, h, left, top, delay, disposal, std::vector<BYTE>((size_t)w * h, bg) };
    return f;
}

static bool Write(IWICImagingFactory* fac, const wchar_t* path,
                  const std::vector<WICColor>& colors, const std::vector<Frame>& frames,
                  USHORT loops, bool transparent) {
    ComPtr<IWICStream> stream; CK(fac->CreateStream(&stream));
    CK(stream->InitializeFromFilename(path, GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> enc;
    CK(fac->CreateEncoder(GUID_ContainerFormatGif, nullptr, &enc));
    CK(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache));

    ComPtr<IWICPalette> pal; CK(fac->CreatePalette(&pal));
    CK(pal->InitializeCustom(const_cast<WICColor*>(colors.data()), (UINT)colors.size()));

    ComPtr<IWICMetadataQueryWriter> gq;
    if (SUCCEEDED(enc->GetMetadataQueryWriter(&gq))) {
        PROPVARIANT v; PropVariantInit(&v);
        BYTE app[11] = { 'N','E','T','S','C','A','P','E','2','.','0' };
        v.vt = VT_UI1 | VT_VECTOR; v.caub.cElems = 11; v.caub.pElems = app;
        gq->SetMetadataByName(L"/appext/Application", &v);
        BYTE data[5] = { 3, 1, (BYTE)(loops & 0xFF), (BYTE)(loops >> 8), 0 };
        v.caub.cElems = 5; v.caub.pElems = data;
        gq->SetMetadataByName(L"/appext/Data", &v);
    }

    for (const Frame& f : frames) {
        ComPtr<IWICBitmapFrameEncode> fe;
        CK(enc->CreateNewFrame(&fe, nullptr));
        CK(fe->Initialize(nullptr));
        CK(fe->SetSize(f.w, f.h));
        WICPixelFormatGUID pf = GUID_WICPixelFormat8bppIndexed;
        CK(fe->SetPixelFormat(&pf));
        CK(fe->SetPalette(pal.Get()));

        ComPtr<IWICMetadataQueryWriter> fq;
        if (SUCCEEDED(fe->GetMetadataQueryWriter(&fq))) {
            PROPVARIANT v; PropVariantInit(&v);
            v.vt = VT_UI2; v.uiVal = f.delay;   fq->SetMetadataByName(L"/grctlext/Delay", &v);
            v.vt = VT_UI1; v.bVal = f.disposal; fq->SetMetadataByName(L"/grctlext/Disposal", &v);
            v.vt = VT_UI2; v.uiVal = (USHORT)f.left; fq->SetMetadataByName(L"/imgdesc/Left", &v);
            v.vt = VT_UI2; v.uiVal = (USHORT)f.top;  fq->SetMetadataByName(L"/imgdesc/Top", &v);
            if (transparent) {
                v.vt = VT_BOOL; v.boolVal = VARIANT_TRUE;
                fq->SetMetadataByName(L"/grctlext/TransparencyFlag", &v);
                v.vt = VT_UI1; v.bVal = 0;      // Index 0 gilt als durchsichtig
                fq->SetMetadataByName(L"/grctlext/TransparentColorIndex", &v);
            }
        }
        CK(fe->WritePixels(f.h, f.w, (UINT)f.px.size(), const_cast<BYTE*>(f.px.data())));
        CK(fe->Commit());
    }
    CK(enc->Commit());
    return true;
}

static void Report(IWICImagingFactory* fac, const wchar_t* path) {
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(fac->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
                                              WICDecodeMetadataCacheOnDemand, &dec))) {
        wprintf(L"  %s: nicht lesbar\n", path); return;
    }
    UINT n = 0; dec->GetFrameCount(&n);
    ComPtr<IWICMetadataQueryReader> gq; dec->GetMetadataQueryReader(&gq);

    auto ui = [&](IWICMetadataQueryReader* q, const wchar_t* name) -> int {
        PROPVARIANT v; PropVariantInit(&v);
        int r = -1;
        if (q && SUCCEEDED(q->GetMetadataByName(name, &v))) {
            if (v.vt == VT_UI2) r = v.uiVal; else if (v.vt == VT_UI1) r = v.bVal;
        }
        PropVariantClear(&v); return r;
    };

    wprintf(L"\n%s  Frames=%u  Leinwand=%dx%d", path, n,
        ui(gq.Get(), L"/logscrdesc/Width"), ui(gq.Get(), L"/logscrdesc/Height"));

    PROPVARIANT d; PropVariantInit(&d);
    if (gq && SUCCEEDED(gq->GetMetadataByName(L"/appext/Data", &d)) &&
        d.vt == (VT_UI1 | VT_VECTOR) && d.caub.cElems >= 4) {
        wprintf(L"  Data=[%u]", d.caub.cElems);
        for (ULONG i = 0; i < d.caub.cElems; i++) wprintf(L" %02X", d.caub.pElems[i]);
        wprintf(L" -> Loops=%u", (UINT)d.caub.pElems[2] | ((UINT)d.caub.pElems[3] << 8));
    }
    PropVariantClear(&d);
    wprintf(L"\n");

    for (UINT i = 0; i < n; i++) {
        ComPtr<IWICBitmapFrameDecode> fr;
        if (FAILED(dec->GetFrame(i, &fr))) continue;
        UINT fw = 0, fh = 0; fr->GetSize(&fw, &fh);
        ComPtr<IWICMetadataQueryReader> fq; fr->GetMetadataQueryReader(&fq);

        // Wie viele Pixel kommen wirklich durchsichtig zurück? Genau darauf
        // stützt sich die Überlagerung im GifAnimator.
        ULONG clear = 0, total = 0;
        ComPtr<IWICFormatConverter> conv;
        if (SUCCEEDED(fac->CreateFormatConverter(&conv)) &&
            SUCCEEDED(conv->Initialize(fr.Get(), GUID_WICPixelFormat32bppPBGRA,
                                       WICBitmapDitherTypeNone, nullptr, 0.0,
                                       WICBitmapPaletteTypeCustom))) {
            std::vector<BYTE> buf((size_t)fw * fh * 4);
            if (SUCCEEDED(conv->CopyPixels(nullptr, fw * 4, (UINT)buf.size(), buf.data()))) {
                total = fw * fh;
                for (ULONG p = 0; p < total; p++) if (buf[p * 4 + 3] == 0) clear++;
            }
        }
        wprintf(L"  Frame %u: %ux%u bei (%d,%d)  Delay=%d  Disposal=%d  Transp=%d  "
                L"durchsichtig %lu/%lu\n",
            i, fw, fh, ui(fq.Get(), L"/imgdesc/Left"), ui(fq.Get(), L"/imgdesc/Top"),
            ui(fq.Get(), L"/grctlext/Delay"), ui(fq.Get(), L"/grctlext/Disposal"),
            ui(fq.Get(), L"/grctlext/TransparencyFlag"), clear, total);
    }
}

int wmain() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ComPtr<IWICImagingFactory> f;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&f)))) return 1;

    // 0 durchsichtig, 1 blau, 2 rot, 3 grün, 4 weiß, 5 orange
    std::vector<WICColor> pal = { 0xFF000000, 0xFF1B3A6B, 0xFFD03030, 0xFF30A050,
                                  0xFFF0F0F0, 0xFFE08020 };

    // -- anim.gif: der einfachste Fall. Ein volles Einzelbild, dann zwei
    //    Teilrechtecke an verschiedenen Stellen, mit ungleichen Verzögerungen
    //    (500, 250, 1000 ms). Werden die Zeiten nicht beachtet, läuft es
    //    sichtbar gleichmäßig.
    {
        std::vector<Frame> fr;
        Frame a0 = Make(64, 64, 0, 0, 50, 2, 1);
        Fill(a0, 0, 48, 64, 16, 5);
        fr.push_back(a0);
        Frame a1 = Make(32, 32, 8, 8, 25, 2, 2);
        fr.push_back(a1);
        Frame a2 = Make(32, 32, 20, 20, 100, 2, 3);
        fr.push_back(a2);
        Write(f.Get(), L"anim.gif", pal, fr, 0, false);
    }

    // -- spur.gif: alle Frames "stehenlassen" (Disposal 1). Nach Frame N müssen
    //    genau N+1 Quadrate zu sehen sein; ein durchsichtiger Rand um jedes
    //    Quadrat darf den Hintergrund nicht überschreiben.
    {
        std::vector<Frame> fr;
        Frame f0 = Make(240, 160, 0, 0, 10, 1, 1);
        Fill(f0, 0, 144, 240, 16, 5);          // Balken unten als fester Bezug
        Fill(f0, 12, 64, 32, 32, 4);           // erstes Quadrat
        fr.push_back(f0);
        for (int i = 1; i < 5; i++) {
            Frame fi = Make(40, 40, 8 + i * 45, 60, 10, 1, 0);   // Rand durchsichtig
            Fill(fi, 4, 4, 32, 32, 4);
            fr.push_back(fi);
        }
        Write(f.Get(), L"spur.gif", pal, fr, 0, true);
    }

    // -- disposal.gif: die drei Aufräumregeln nacheinander.
    {
        std::vector<Frame> fr;
        fr.push_back(Make(240, 160, 0, 0, 40, 1, 1));            // ganz blau, stehenlassen
        Frame f1 = Make(40, 40, 20, 60, 40, 2, 2); fr.push_back(f1);   // rot, auf Hintergrund
        Frame f2 = Make(40, 40, 100, 60, 40, 3, 3); fr.push_back(f2);  // grün, auf vorigen Stand
        Frame f3 = Make(40, 40, 180, 60, 40, 1, 4); fr.push_back(f3);  // weiß, stehenlassen
        Write(f.Get(), L"disposal.gif", pal, fr, 0, true);
    }

    // -- zweimal.gif: endlicher Wiederholungszähler.
    {
        std::vector<Frame> fr;
        for (int i = 0; i < 3; i++)
            fr.push_back(Make(160, 100, 0, 0, 20, 1, (BYTE)(i + 2)));
        Write(f.Get(), L"zweimal.gif", pal, fr, 2, false);
    }

    // -- gross_a/gross_b: dieselbe Bauart beidseits der 256-MB-Grenze.
    //    3400*1000*4 = 13,6 MB je Leinwand. 19 Frames = 258 MB (darunter, wird
    //    zwischengespeichert), 20 Frames = 272 MB (darüber, wird fortlaufend
    //    komponiert). Der Unterschied muss sich am Speicherbedarf ablesen lassen.
    for (int variant = 0; variant < 2; variant++) {
        const int count = 19 + variant;
        std::vector<Frame> fr;
        Frame f0 = Make(3400, 1000, 0, 0, 5, 1, 1);
        Fill(f0, 0, 900, 3400, 100, 5);
        Fill(f0, 20, 430, 140, 140, 4);
        fr.push_back(f0);
        for (int i = 1; i < count; i++) {
            Frame fi = Make(140, 140, 20 + i * 170, 430, 5, 1, 0);
            Fill(fi, 0, 0, 140, 140, 4);
            fr.push_back(fi);
        }
        Write(f.Get(), variant == 0 ? L"gross_a.gif" : L"gross_b.gif", pal, fr, 0, true);
    }

    Report(f.Get(), L"anim.gif");
    Report(f.Get(), L"spur.gif");
    Report(f.Get(), L"disposal.gif");
    Report(f.Get(), L"zweimal.gif");
    return 0;
}
