// Was steht wirklich in einem GIF, und als welcher Datentyp?
//
//     gifdaten.exe ..\..\testdata\anim.gif
//
// Liest die Metadaten Feld fuer Feld aus und gibt neben dem Wert auch den
// Variantentyp aus. Genau daran haengt Abschnitt 5 von PLAN.md: die
// Verzoegerung kommt als VT_UI2 und in Hundertstelsekunden, die Aufraeumregel
// als VT_UI1, und /imgdesc/Width ist etwas anderes als IWICBitmapFrameDecode::
// GetSize -- wer beides verwechselt, komponiert Teilrechtecke an die falsche
// Stelle.

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstdio>
#include <vector>
using Microsoft::WRL::ComPtr;

#define CK(x) do{ HRESULT _h=(x); if(FAILED(_h)){ wprintf(L"FAIL %S -> 0x%08lX\n",#x,(unsigned long)_h); return 1; } }while(0)

static void rd(IWICMetadataQueryReader* q, const wchar_t* name) {
    PROPVARIANT v; PropVariantInit(&v);
    HRESULT h = q->GetMetadataByName(name, &v);
    if (FAILED(h)) { wprintf(L"    %-34s FEHLT (0x%08lX)\n", name, (unsigned long)h); return; }
    wprintf(L"    %-34s vt=%-3u ", name, v.vt);
    if (v.vt == VT_UI2) wprintf(L"= %u", v.uiVal);
    else if (v.vt == VT_UI1) wprintf(L"= %u", v.bVal);
    else if (v.vt == VT_BOOL) wprintf(L"= %s", v.boolVal ? L"true" : L"false");
    else if (v.vt == (VT_UI1 | VT_VECTOR)) {
        wprintf(L"= [%u bytes] ", v.caub.cElems);
        for (ULONG i = 0; i < v.caub.cElems && i < 16; i++) wprintf(L"%02X ", v.caub.pElems[i]);
    }
    wprintf(L"\n");
    PropVariantClear(&v);
}

int wmain() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ComPtr<IWICImagingFactory> f;
    CK(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)));

    struct Spec { UINT w, h, left, top; USHORT delay; BYTE idx; };
    Spec specs[3] = { {64,64,0,0,50,1}, {32,32,8,8,25,2}, {32,32,20,20,100,3} };
    const wchar_t* path = L"anim.gif";

    wprintf(L"=== ENCODE ===\n");
    ComPtr<IWICStream> stream; CK(f->CreateStream(&stream));
    CK(stream->InitializeFromFilename(path, GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> enc;
    CK(f->CreateEncoder(GUID_ContainerFormatGif, nullptr, &enc));
    CK(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache));

    WICColor colors[4] = { 0xFF000000, 0xFFFF0000, 0xFF00FF00, 0xFF0000FF };
    ComPtr<IWICPalette> pal; CK(f->CreatePalette(&pal));
    CK(pal->InitializeCustom(colors, 4));

    {   // NETSCAPE2.0 Loop-Extension auf Container-Ebene
        ComPtr<IWICMetadataQueryWriter> qw;
        HRESULT h = enc->GetMetadataQueryWriter(&qw);
        wprintf(L"  encoder query writer -> 0x%08lX\n", (unsigned long)h);
        if (SUCCEEDED(h)) {
            PROPVARIANT v; PropVariantInit(&v);
            BYTE app[11] = { 'N','E','T','S','C','A','P','E','2','.','0' };
            v.vt = VT_UI1 | VT_VECTOR; v.caub.cElems = 11; v.caub.pElems = app;
            HRESULT h1 = qw->SetMetadataByName(L"/appext/Application", &v);
            BYTE data[5] = { 3, 1, 0, 0, 0 };   // Sub-Block: len=3, id=1, loop=0 (endlos, LE)
            v.caub.cElems = 5; v.caub.pElems = data;
            HRESULT h2 = qw->SetMetadataByName(L"/appext/Data", &v);
            wprintf(L"  /appext/Application -> 0x%08lX  /appext/Data -> 0x%08lX\n",
                (unsigned long)h1, (unsigned long)h2);
        }
    }

    for (int i = 0; i < 3; i++) {
        Spec& s = specs[i];
        ComPtr<IWICBitmapFrameEncode> fe;
        CK(enc->CreateNewFrame(&fe, nullptr));
        CK(fe->Initialize(nullptr));
        CK(fe->SetSize(s.w, s.h));
        WICPixelFormatGUID pf = GUID_WICPixelFormat8bppIndexed;
        CK(fe->SetPixelFormat(&pf));
        CK(fe->SetPalette(pal.Get()));
        ComPtr<IWICMetadataQueryWriter> fq;
        HRESULT hq = fe->GetMetadataQueryWriter(&fq);
        if (SUCCEEDED(hq)) {
            PROPVARIANT v; PropVariantInit(&v);
            v.vt = VT_UI2; v.uiVal = s.delay;
            HRESULT a = fq->SetMetadataByName(L"/grctlext/Delay", &v);
            v.vt = VT_UI1; v.bVal = 2;                       // Disposal: restore to background
            HRESULT b = fq->SetMetadataByName(L"/grctlext/Disposal", &v);
            v.vt = VT_UI2; v.uiVal = (USHORT)s.left;
            HRESULT c = fq->SetMetadataByName(L"/imgdesc/Left", &v);
            v.uiVal = (USHORT)s.top;
            HRESULT d = fq->SetMetadataByName(L"/imgdesc/Top", &v);
            wprintf(L"  frame %d: Delay->0x%08lX Disposal->0x%08lX Left->0x%08lX Top->0x%08lX\n",
                i, (unsigned long)a, (unsigned long)b, (unsigned long)c, (unsigned long)d);
        }
        std::vector<BYTE> buf((size_t)s.w * s.h, s.idx);
        CK(fe->WritePixels(s.h, s.w, (UINT)buf.size(), buf.data()));
        CK(fe->Commit());
    }
    CK(enc->Commit());
    enc.Reset();        // Encoder haelt eine Referenz auf den Stream -> zuerst freigeben
    stream.Reset();

    wprintf(L"\n=== DECODE ===\n");
    ComPtr<IWICBitmapDecoder> dec;
    CK(f->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec));
    UINT n = 0; CK(dec->GetFrameCount(&n));
    wprintf(L"  GetFrameCount = %u\n", n);

    ComPtr<IWICMetadataQueryReader> gq;
    if (SUCCEEDED(dec->GetMetadataQueryReader(&gq))) {
        wprintf(L"  global:\n");
        rd(gq.Get(), L"/logscrdesc/Width");
        rd(gq.Get(), L"/logscrdesc/Height");
        rd(gq.Get(), L"/logscrdesc/GlobalColorTableFlag");
        rd(gq.Get(), L"/logscrdesc/BackgroundColorIndex");
        rd(gq.Get(), L"/appext/Application");
        rd(gq.Get(), L"/appext/Data");
    }
    for (UINT i = 0; i < n; i++) {
        ComPtr<IWICBitmapFrameDecode> fr;
        CK(dec->GetFrame(i, &fr));
        UINT fw = 0, fh = 0; fr->GetSize(&fw, &fh);
        wprintf(L"  frame %u: GetSize = %ux%u\n", i, fw, fh);
        ComPtr<IWICMetadataQueryReader> fq;
        if (SUCCEEDED(fr->GetMetadataQueryReader(&fq))) {
            rd(fq.Get(), L"/grctlext/Delay");
            rd(fq.Get(), L"/grctlext/Disposal");
            rd(fq.Get(), L"/grctlext/TransparencyFlag");
            rd(fq.Get(), L"/imgdesc/Left");
            rd(fq.Get(), L"/imgdesc/Top");
            rd(fq.Get(), L"/imgdesc/Width");
            rd(fq.Get(), L"/imgdesc/Height");
        }
    }
    return 0;
}
