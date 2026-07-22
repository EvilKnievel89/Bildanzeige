// Was kommt beim Drucken tatsächlich heraus?
//
//     drucken.exe ..\..\testdata\gross.png ..\..\testdata\mehrseitig.tif
//     drucken.exe -drucker "Microsoft Print to PDF" ..\..\testdata\dreh1.jpg
//
// Der Druckweg lässt sich nicht von Hand nachrechnen: was ein Treiber als
// bedruckbaren Bereich meldet und wie weit der vom Blattrand absteht, weiß
// man erst, wenn man ihn fragt. Dieses Programm fragt jeden eingerichteten
// Drucker, rechnet dann mit *derselben* Funktion, die auch wirklich druckt
// (PrintPageToDC aus src/Printer.cpp), und prüft drei Dinge nach:
//
//   1. die Gerätemaße -- Beleg für den unsymmetrischen Rand, um dessen
//      willen auf dem Blatt zentriert wird und nicht im bedruckbaren Bereich
//   2. einen echten Auftrag nach PDF: Seitenverhältnis, Lage, Zentrierung
//   3. den weißen Grund: eine halb durchsichtige Vorlage durch denselben
//      Weg geschickt und hinterher nachgesehen, ob aus dem Durchsichtigen
//      Weiß geworden ist und nicht Schwarz
//
// PLAN.md, Abschnitt 9.

#include <windows.h>
#include <winspool.h>

#include "ImageDocument.h"
#include "Printer.h"

#include <wincodec.h>

#include <clocale>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace
{
    int fehler = 0;

    void Pruefe(bool ok, const wchar_t* text)
    {
        wprintf(L"    [%s] %s\n", ok ? L"ok " : L"!! ", text);
        if (!ok)
            fehler++;
    }

    std::vector<std::wstring> Drucker()
    {
        std::vector<std::wstring> namen;
        DWORD bytes = 0, count = 0;
        EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, nullptr, 4, nullptr, 0,
                      &bytes, &count);
        if (bytes == 0)
            return namen;

        std::vector<BYTE> puffer(bytes);
        if (!EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, nullptr, 4,
                           puffer.data(), bytes, &bytes, &count))
            return namen;

        const auto* info = reinterpret_cast<const PRINTER_INFO_4W*>(puffer.data());
        for (DWORD i = 0; i < count; i++)
            namen.push_back(info[i].pPrinterName);
        return namen;
    }

    // Teil 1: was die Geräte über sich sagen.
    void ZeigeGeraete(const std::vector<std::wstring>& namen)
    {
        wprintf(L"\n  Gerätemaße (Punkte des Geräts)\n");
        wprintf(L"  %-34s %5s %11s %11s %9s\n", L"Drucker", L"dpi", L"Blatt", L"bedruckbar",
                L"Rand l/o");
        for (const std::wstring& name : namen)
        {
            HDC dc = CreateDCW(nullptr, name.c_str(), nullptr, nullptr);
            if (dc == nullptr)
            {
                wprintf(L"  %-34s  (kein Gerätekontext)\n", name.c_str());
                continue;
            }

            const int sheetW = GetDeviceCaps(dc, PHYSICALWIDTH);
            const int sheetH = GetDeviceCaps(dc, PHYSICALHEIGHT);
            const int areaW = GetDeviceCaps(dc, HORZRES);
            const int areaH = GetDeviceCaps(dc, VERTRES);
            const int offX = GetDeviceCaps(dc, PHYSICALOFFSETX);
            const int offY = GetDeviceCaps(dc, PHYSICALOFFSETY);
            wprintf(L"  %-34.34s %5d %5d x%5d %5d x%5d %4d/%4d", name.c_str(),
                    GetDeviceCaps(dc, LOGPIXELSX), sheetW, sheetH, areaW, areaH, offX, offY);

            // Der Rand rechts und unten ergibt sich aus dem Rest. Sind die
            // beiden Zahlenpaare verschieden, ist der Rand unsymmetrisch --
            // genau der Fall, für den FitOnSheet auf dem Blatt zentriert.
            if (sheetW > 0 && sheetH > 0)
            {
                const int restX = sheetW - areaW - offX;
                const int restY = sheetH - areaH - offY;
                wprintf(L"  r/u %d/%d%s", restX, restY,
                        (restX != offX || restY != offY) ? L"  unsymmetrisch" : L"");
            }
            wprintf(L"\n");
            DeleteDC(dc);
        }
    }

    // Teil 2: ein echter Auftrag, Seite für Seite nachgemessen.
    void PruefeAuftrag(IWICImagingFactory* wic, const std::wstring& drucker,
                       const std::wstring& datei, int drehung, const std::wstring& ziel)
    {
        std::wstring kopf = datei;
        if (drehung != 0)
            kopf += L"   (um " + std::to_wstring(drehung * 90) + L" Grad gedreht)";
        wprintf(L"\n  %s\n", kopf.c_str());

        std::wstring meldung;
        ImageDocument dokument;
        if (!dokument.Open(wic, datei, meldung))
        {
            wprintf(L"    öffnen fehlgeschlagen: %s\n", meldung.c_str());
            fehler++;
            return;
        }

        HDC dc = CreateDCW(nullptr, drucker.c_str(), nullptr, nullptr);
        if (dc == nullptr)
        {
            wprintf(L"    kein Gerätekontext für \"%s\"\n", drucker.c_str());
            fehler++;
            return;
        }

        const int areaW = GetDeviceCaps(dc, HORZRES);
        const int areaH = GetDeviceCaps(dc, VERTRES);
        int sheetW = GetDeviceCaps(dc, PHYSICALWIDTH);
        int sheetH = GetDeviceCaps(dc, PHYSICALHEIGHT);
        int offX = GetDeviceCaps(dc, PHYSICALOFFSETX);
        int offY = GetDeviceCaps(dc, PHYSICALOFFSETY);
        if (sheetW <= 0 || sheetH <= 0)
        {
            sheetW = areaW;
            sheetH = areaH;
            offX = 0;
            offY = 0;
        }

        DOCINFOW info{};
        info.cbSize = sizeof(info);
        info.lpszDocName = L"Bildanzeige-Prüfung";
        info.lpszOutput = ziel.c_str();   // ohne das fragte der PDF-Treiber nach dem Namen

        if (StartDocW(dc, &info) <= 0)
        {
            wprintf(L"    StartDoc abgelehnt\n");
            fehler++;
            DeleteDC(dc);
            return;
        }

        for (UINT seite = 0; seite < dokument.FrameCount(); seite++)
        {
            ComPtr<IWICBitmapSource> quelle = dokument.LoadFrame(wic, seite, meldung);
            if (!quelle)
            {
                wprintf(L"    Seite %u: %s\n", seite + 1, meldung.c_str());
                fehler++;
                continue;
            }

            UINT bildW = 0, bildH = 0;
            quelle->GetSize(&bildW, &bildH);
            if (drehung % 2 != 0)
                std::swap(bildW, bildH);

            StartPage(dc);
            PrintPlacement platz;
            const bool ok = PrintPageToDC(dc, wic, quelle.Get(), drehung, kPrintDpi, &platz,
                                          meldung);
            EndPage(dc);

            if (!ok)
            {
                wprintf(L"    Seite %u: %s\n", seite + 1, meldung.c_str());
                fehler++;
                continue;
            }

            wprintf(L"    Seite %u: %5u x %-5u  ->  %d x %d  bei (%d, %d)\n", seite + 1, bildW,
                    bildH, platz.width, platz.height, platz.x, platz.y);

            // Seitenverhältnis: das Bild darf nicht verzerrt aufs Blatt.
            const double sollte = static_cast<double>(bildW) / bildH;
            const double ist = static_cast<double>(platz.width) / platz.height;
            Pruefe(std::fabs(sollte - ist) / sollte < 0.002, L"Seitenverhältnis bleibt erhalten");

            // Es muss ausgefüllt sein: eine Kante berührt den Rand.
            Pruefe(platz.width == areaW || platz.height == areaH,
                   L"eingepasst -- eine Kante stößt an den bedruckbaren Rand");

            Pruefe(platz.x >= 0 && platz.y >= 0 && platz.x + platz.width <= areaW &&
                       platz.y + platz.height <= areaH,
                   L"liegt vollständig im bedruckbaren Bereich");

            // Zentriert auf dem *Blatt*: die Mitte des Bildes muss auf der
            // Mitte des Blattes liegen, nicht auf der des bedruckbaren
            // Bereichs. Beide unterscheiden sich um den halben Randunterschied.
            const int mitteBlattX = sheetW / 2 - offX;
            const int mitteBlattY = sheetH / 2 - offY;
            const int mitteBildX = platz.x + platz.width / 2;
            const int mitteBildY = platz.y + platz.height / 2;
            wprintf(L"      Mitte Bild (%d, %d) gegen Mitte Blatt (%d, %d)\n", mitteBildX,
                    mitteBildY, mitteBlattX, mitteBlattY);
            Pruefe(std::abs(mitteBildX - mitteBlattX) <= 1 &&
                       std::abs(mitteBildY - mitteBlattY) <= 1,
                   L"auf dem Blatt zentriert, nicht im bedruckbaren Bereich");
        }

        EndDoc(dc);
        DeleteDC(dc);
    }

    // Teil 3: der weiße Grund.
    //
    // Gezeichnet wird in eine Metadatei mit dem Drucker als Bezugsgerät --
    // PrintPageToDC rechnet also mit den echten Maßen des Geräts --, und die
    // Metadatei wird hinterher in eine Bitmap zurückgespielt. Erst dadurch
    // sind die Punkte zu sehen, die sonst im Treiber verschwinden.
    void PruefeWeissenGrund(IWICImagingFactory* wic, const std::wstring& drucker)
    {
        wprintf(L"\n  Weißer Grund unter Durchsichtigem\n");

        // Vorlage: links deckendes Rot, rechts völlig durchsichtig. In
        // vormultiplizierten Werten steht rechts lauter Null -- ohne das
        // Unterlegen käme dort Schwarz heraus.
        constexpr UINT breite = 200, hoehe = 100;
        ComPtr<IWICBitmap> vorlage;
        if (FAILED(wic->CreateBitmap(breite, hoehe, GUID_WICPixelFormat32bppPBGRA,
                                     WICBitmapCacheOnLoad, &vorlage)))
        {
            wprintf(L"    Vorlage ließ sich nicht anlegen\n");
            fehler++;
            return;
        }
        {
            ComPtr<IWICBitmapLock> sperre;
            WICRect alles = { 0, 0, static_cast<INT>(breite), static_cast<INT>(hoehe) };
            if (FAILED(vorlage->Lock(&alles, WICBitmapLockWrite, &sperre)))
            {
                fehler++;
                return;
            }
            UINT schritt = 0, groesse = 0;
            BYTE* punkte = nullptr;
            sperre->GetStride(&schritt);
            sperre->GetDataPointer(&groesse, &punkte);
            for (UINT y = 0; y < hoehe; y++)
            {
                BYTE* zeile = punkte + static_cast<size_t>(y) * schritt;
                for (UINT x = 0; x < breite; x++)
                {
                    const bool deckend = x < breite / 2;
                    zeile[x * 4 + 0] = 0;                                  // B
                    zeile[x * 4 + 1] = 0;                                  // G
                    zeile[x * 4 + 2] = deckend ? BYTE{ 255 } : BYTE{ 0 };  // R
                    zeile[x * 4 + 3] = deckend ? BYTE{ 255 } : BYTE{ 0 };  // A
                }
            }
        }

        HDC bezug = CreateDCW(nullptr, drucker.c_str(), nullptr, nullptr);
        if (bezug == nullptr)
        {
            wprintf(L"    kein Gerätekontext für \"%s\"\n", drucker.c_str());
            fehler++;
            return;
        }

        // Ohne den Rahmen setzt GDI ihn auf die Umrisse des Gezeichneten, und
        // das Bild füllte beim Abspielen die ganze Zielfläche -- die Lage auf
        // dem Blatt wäre weg. Am magentafarbenen Rest unten ist zu sehen, dass
        // sie erhalten bleibt.
        const RECT rahmen = PrintMetafileFrame(bezug);
        HDC meta = CreateEnhMetaFileW(bezug, nullptr, &rahmen, nullptr);
        std::wstring meldung;
        ComPtr<IWICBitmapSource> quelle;
        vorlage.As(&quelle);
        const bool gezeichnet = PrintPageToDC(meta, wic, quelle.Get(), 0, kPrintDpi, nullptr,
                                              meldung);
        HENHMETAFILE aufzeichnung = CloseEnhMetaFile(meta);
        DeleteDC(bezug);

        if (!gezeichnet || aufzeichnung == nullptr)
        {
            wprintf(L"    zeichnen fehlgeschlagen: %s\n", meldung.c_str());
            fehler++;
            if (aufzeichnung != nullptr)
                DeleteEnhMetaFile(aufzeichnung);
            return;
        }

        // Zurücklesen: die Aufzeichnung in eine kleine Bitmap abspielen. Der
        // Grund wird vorher magenta gefärbt -- so ist "nicht bemalt" von
        // "weiß bemalt" zu unterscheiden.
        constexpr int zielB = 600, zielH = 800;
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = zielB;
        bi.bmiHeader.biHeight = -zielH;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        void* punkte = nullptr;
        HDC speicher = CreateCompatibleDC(nullptr);
        HBITMAP flaeche = CreateDIBSection(speicher, &bi, DIB_RGB_COLORS, &punkte, nullptr, 0);
        HGDIOBJ vorher = SelectObject(speicher, flaeche);
        memset(punkte, 0, static_cast<size_t>(zielB) * zielH * 4);
        for (int i = 0; i < zielB * zielH; i++)
        {
            static_cast<BYTE*>(punkte)[i * 4 + 0] = 255;   // B
            static_cast<BYTE*>(punkte)[i * 4 + 2] = 255;   // R  -> magenta
        }

        RECT ziel = { 0, 0, zielB, zielH };
        PlayEnhMetaFile(speicher, aufzeichnung, &ziel);
        GdiFlush();

        int rot = 0, weiß = 0, schwarz = 0, magenta = 0;
        for (int i = 0; i < zielB * zielH; i++)
        {
            const BYTE b = static_cast<BYTE*>(punkte)[i * 4 + 0];
            const BYTE g = static_cast<BYTE*>(punkte)[i * 4 + 1];
            const BYTE r = static_cast<BYTE*>(punkte)[i * 4 + 2];
            if (r > 200 && g < 60 && b < 60)
                rot++;
            else if (r > 240 && g > 240 && b > 240)
                weiß++;
            else if (r < 40 && g < 40 && b < 40)
                schwarz++;
            else if (r > 200 && b > 200 && g < 60)
                magenta++;
        }
        wprintf(L"    rot %d   weiß %d   schwarz %d   unbemalt %d\n", rot, weiß, schwarz,
                magenta);

        Pruefe(schwarz == 0, L"kein einziger schwarzer Punkt -- das Durchsichtige wurde "
                             L"unterlegt");
        Pruefe(weiß > 0 && rot > 0, L"beide Hälften sind angekommen");
        Pruefe(rot > 0 && std::abs(rot - weiß) < rot / 5,
               L"die durchsichtige Hälfte ist so groß wie die deckende");
        Pruefe(magenta > 0, L"unbemalter Rest vorhanden -- die Einpassung ist nicht "
                            L"aufgebläht worden");

        SelectObject(speicher, vorher);
        DeleteObject(flaeche);
        DeleteDC(speicher);
        DeleteEnhMetaFile(aufzeichnung);
    }
}

int wmain(int argc, wchar_t** argv)
{
    setlocale(LC_ALL, ".UTF8");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    std::wstring drucker = L"Microsoft Print to PDF";
    std::vector<std::wstring> dateien;
    for (int a = 1; a < argc; a++)
    {
        if (wcscmp(argv[a], L"-drucker") == 0 && a + 1 < argc)
            drucker = argv[++a];
        else
            dateien.push_back(argv[a]);
    }

    const std::vector<std::wstring> namen = Drucker();
    if (namen.empty())
    {
        wprintf(L"  Kein Drucker eingerichtet -- ohne einen ist hier nichts zu messen.\n");
        return 0;
    }
    ZeigeGeraete(namen);

    bool vorhanden = false;
    for (const std::wstring& name : namen)
        vorhanden = vorhanden || name == drucker;
    if (!vorhanden)
    {
        wprintf(L"\n  \"%s\" ist nicht eingerichtet; genommen wird \"%s\".\n", drucker.c_str(),
                namen[0].c_str());
        drucker = namen[0];
    }

    {
        ComPtr<IWICImagingFactory> wic;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(&wic));

        wprintf(L"\n  Aufträge an \"%s\"\n", drucker.c_str());
        for (size_t i = 0; i < dateien.size(); i++)
        {
            // Die letzte Datei wird gedreht gedruckt -- damit läuft auch der
            // Weg durch den IWICBitmapFlipRotator einmal mit.
            const int drehung = (i + 1 == dateien.size()) ? 1 : 0;
            PruefeAuftrag(wic.Get(), drucker, dateien[i], drehung,
                          L"drucken-probe-" + std::to_wstring(i) + L".pdf");
        }

        PruefeWeissenGrund(wic.Get(), drucker);
    }

    wprintf(L"\n  %d Beanstandung(en)\n", fehler);
    CoUninitialize();
    return fehler;
}
