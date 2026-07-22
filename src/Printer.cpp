#include "Printer.h"

#include "ImageDocument.h"

#include <commdlg.h>
#include <shlwapi.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <new>
#include <vector>

namespace
{
    // Feiner als das Geraet zu rechnen bringt nichts aufs Papier, und jenseits
    // von 600 dpi ist am Blatt kein Unterschied mehr zu sehen. Treiber, die
    // 1200 oder 2400 dpi melden, sind haeufig -- ohne diese Grenze legte ein
    // A4-Auftrag dort ein halbes Gigabyte an, fuer nichts.
    constexpr int kMaxRenderDpi = 600;

    // Zweite Grenze, fuer grosses Papier: A0 bei 600 dpi waeren 550 Megapixel.
    // 36 Millionen sind A4 bei 600 dpi mit Luft -- der Fall, um den es geht.
    constexpr double kMaxRenderPixels = 36'000'000.0;

    // Platz des Bildes auf dem Blatt: eingepasst unter Wahrung des
    // Seitenverhaeltnisses und auf dem *Blatt* zentriert, nicht im bedruckbaren
    // Bereich. Der ist bei den meisten Geraeten unsymmetrisch -- unten bleibt
    // fuer den Einzug mehr Rand. Wer darin zentriert, bekommt ein Bild, das auf
    // dem fertigen Blatt sichtbar zu hoch sitzt.
    PrintPlacement FitOnSheet(HDC dc, UINT imageWidth, UINT imageHeight)
    {
        PrintPlacement place;

        const int areaWidth = GetDeviceCaps(dc, HORZRES);
        const int areaHeight = GetDeviceCaps(dc, VERTRES);
        if (areaWidth <= 0 || areaHeight <= 0 || imageWidth == 0 || imageHeight == 0)
            return place;

        const double scale = std::min(static_cast<double>(areaWidth) / imageWidth,
                                      static_cast<double>(areaHeight) / imageHeight);
        place.width = std::clamp(static_cast<int>(std::lround(imageWidth * scale)), 1, areaWidth);
        place.height = std::clamp(static_cast<int>(std::lround(imageHeight * scale)), 1, areaHeight);

        // Der Ursprung des Geraets liegt in der linken oberen Ecke des
        // *bedruckbaren* Bereichs; das Blatt faengt um PHYSICALOFFSET frueher an.
        int sheetWidth = GetDeviceCaps(dc, PHYSICALWIDTH);
        int sheetHeight = GetDeviceCaps(dc, PHYSICALHEIGHT);
        int offsetX = GetDeviceCaps(dc, PHYSICALOFFSETX);
        int offsetY = GetDeviceCaps(dc, PHYSICALOFFSETY);
        if (sheetWidth <= 0 || sheetHeight <= 0)
        {
            // Treiber ohne Angaben zum Blatt -- etwa die Ausgabe in eine Datei.
            // Dann bleibt nur der bedruckbare Bereich als Bezug.
            sheetWidth = areaWidth;
            sheetHeight = areaHeight;
            offsetX = 0;
            offsetY = 0;
        }

        place.x = (sheetWidth - place.width) / 2 - offsetX;
        place.y = (sheetHeight - place.height) / 2 - offsetY;

        // Auf dem Blatt zentriert kann heissen: zum Teil im unbedruckbaren Rand.
        place.x = std::clamp(place.x, 0, areaWidth - place.width);
        place.y = std::clamp(place.y, 0, areaHeight - place.height);
        return place;
    }

    // Dieselbe Drehung wie in der Anzeige. WICBitmapTransformRotate90 dreht im
    // Uhrzeigersinn, ebenso wie ViewState zaehlt -- die Viertel gehen also
    // unveraendert durch.
    ComPtr<IWICBitmapSource> Turn(IWICImagingFactory* wic, IWICBitmapSource* source,
                                  int quarters, std::wstring& error)
    {
        const int turns = ((quarters % 4) + 4) % 4;
        if (turns == 0)
            return ComPtr<IWICBitmapSource>(source);

        WICBitmapTransformOptions options = WICBitmapTransformRotate90;
        if (turns == 2)
            options = WICBitmapTransformRotate180;
        else if (turns == 3)
            options = WICBitmapTransformRotate270;

        ComPtr<IWICBitmapFlipRotator> rotator;
        HRESULT hr = wic->CreateBitmapFlipRotator(&rotator);
        if (SUCCEEDED(hr))
            hr = rotator->Initialize(source, options);
        if (FAILED(hr))
        {
            error = L"Das Bild liess sich nicht drehen.\n\n" + FormatHResult(hr);
            return nullptr;
        }

        ComPtr<IWICBitmapSource> turned;
        hr = rotator.As(&turned);
        if (FAILED(hr))
        {
            error = FormatHResult(hr);
            return nullptr;
        }
        return turned;
    }

    ComPtr<IWICBitmapSource> ScaleTo(IWICImagingFactory* wic, IWICBitmapSource* source,
                                     UINT width, UINT height, std::wstring& error)
    {
        ComPtr<IWICBitmapScaler> scaler;
        HRESULT hr = wic->CreateBitmapScaler(&scaler);
        if (SUCCEEDED(hr))
            hr = scaler->Initialize(source, width, height, WICBitmapInterpolationModeFant);
        if (FAILED(hr))
        {
            error = L"Das Bild liess sich nicht auf die Seitengroesse bringen.\n\n" +
                    FormatHResult(hr);
            return nullptr;
        }

        ComPtr<IWICBitmapSource> scaled;
        hr = scaler.As(&scaled);
        if (FAILED(hr))
        {
            error = FormatHResult(hr);
            return nullptr;
        }
        return scaled;
    }

    // Welche Seiten der Dialog bestellt hat, 0-basiert.
    std::vector<UINT> ChosenPages(const PRINTDLGEXW& dialog, UINT pageCount, UINT currentPage,
                                  bool single)
    {
        std::vector<UINT> pages;
        if (single || (dialog.Flags & PD_CURRENTPAGE) != 0)
        {
            pages.push_back(currentPage);
            return pages;
        }

        if ((dialog.Flags & PD_PAGENUMS) != 0 && dialog.lpPageRanges != nullptr)
        {
            for (DWORD i = 0; i < dialog.nPageRanges; ++i)
            {
                const PRINTPAGERANGE& range = dialog.lpPageRanges[i];
                for (DWORD page = range.nFromPage; page <= range.nToPage; ++page)
                {
                    if (page >= 1 && page <= pageCount)
                        pages.push_back(page - 1);
                }
            }
            return pages;
        }

        for (UINT page = 0; page < pageCount; ++page)
            pages.push_back(page);
        return pages;
    }
}

bool PrintPageToDC(HDC dc, IWICImagingFactory* wic, IWICBitmapSource* source, int rotationQuarters,
                   PrintPlacement* placement, std::wstring& error)
{
    ComPtr<IWICBitmapSource> turned = Turn(wic, source, rotationQuarters, error);
    if (!turned)
        return false;

    UINT width = 0;
    UINT height = 0;
    if (FAILED(turned->GetSize(&width, &height)) || width == 0 || height == 0)
    {
        error = L"Das Bild hat keine brauchbare Groesse.";
        return false;
    }

    const PrintPlacement place = FitOnSheet(dc, width, height);
    if (placement != nullptr)
        *placement = place;
    if (place.width <= 0 || place.height <= 0)
    {
        error = L"Der Drucker meldet keinen bedruckbaren Bereich.";
        return false;
    }

    // Gerechnet wird hoechstens so fein, wie das Blatt es aufnimmt, und
    // hoechstens so fein, wie die Quelle es hergibt. Ein kleines Bild wird also
    // nicht hier vergroessert, sondern von StretchDIBits im Treiber -- das
    // spart den Speicher fuer eine Vergroesserung, die nichts hinzufuegt.
    double allowed = static_cast<double>(place.width);
    const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    if (dpi > kMaxRenderDpi)
        allowed = allowed * kMaxRenderDpi / dpi;

    double factor = std::min(1.0, allowed / width);
    const double pixels = (width * factor) * (height * factor);
    if (pixels > kMaxRenderPixels)
        factor *= std::sqrt(kMaxRenderPixels / pixels);

    const UINT renderWidth = std::max(1u, static_cast<UINT>(std::lround(width * factor)));
    const UINT renderHeight = std::max(1u, static_cast<UINT>(std::lround(height * factor)));

    ComPtr<IWICBitmapSource> ready = turned;
    if (renderWidth != width || renderHeight != height)
    {
        ready = ScaleTo(wic, turned.Get(), renderWidth, renderHeight, error);
        if (!ready)
            return false;
    }

    // Ein einziger Puffer fuer beides: erst kommen 32bppPBGRA hinein, dann
    // werden sie an Ort und Stelle zu 24bppBGR zusammengeschoben. Ein zweiter
    // Puffer waere bei 36 Megapixeln noch einmal 100 MB, und zeilenweise zu
    // holen hiesse bei einem JPEG, die Datei je Streifen erneut zu dekodieren.
    const size_t sourceStride = static_cast<size_t>(renderWidth) * 4;
    const size_t sourceBytes = sourceStride * renderHeight;
    std::unique_ptr<BYTE[]> pixelBuffer(new (std::nothrow) BYTE[sourceBytes]);
    if (!pixelBuffer)
    {
        error = L"Fuer den Druck ist nicht genug Speicher frei.";
        return false;
    }

    const HRESULT hr = ready->CopyPixels(nullptr, static_cast<UINT>(sourceStride),
                                         static_cast<UINT>(sourceBytes), pixelBuffer.get());
    if (FAILED(hr))
    {
        error = L"Das Bild liess sich nicht lesen.\n\n" + FormatHResult(hr);
        return false;
    }

    // Papier ist weiss. Ohne das Unterlegen kaeme jede durchsichtige Stelle als
    // Schwarz heraus, denn in vormultiplizierten Werten steht dort eine Null.
    // Ueber Weiss ist die Rechnung denkbar einfach: Wert + (255 - Alpha).
    //
    // Die Zielzeile ist nie laenger als die Quellzeile, und im Zeileninneren
    // laeuft das Ziel um ein Byte je Punkt hinter der Quelle her -- es wird
    // also nichts ueberschrieben, was noch zu lesen waere.
    const size_t targetStride =
        ((static_cast<size_t>(renderWidth) * 3) + 3) & ~static_cast<size_t>(3);
    for (UINT y = 0; y < renderHeight; ++y)
    {
        const BYTE* src = pixelBuffer.get() + static_cast<size_t>(y) * sourceStride;
        BYTE* dst = pixelBuffer.get() + static_cast<size_t>(y) * targetStride;
        for (UINT x = 0; x < renderWidth; ++x)
        {
            const BYTE fill = static_cast<BYTE>(255 - src[3]);
            dst[0] = static_cast<BYTE>(src[0] + fill);
            dst[1] = static_cast<BYTE>(src[1] + fill);
            dst[2] = static_cast<BYTE>(src[2] + fill);
            src += 4;
            dst += 3;
        }
    }

    BITMAPINFOHEADER header{};
    header.biSize = sizeof(header);
    header.biWidth = static_cast<LONG>(renderWidth);
    header.biHeight = -static_cast<LONG>(renderHeight);   // negativ: von oben nach unten
    header.biPlanes = 1;
    header.biBitCount = 24;
    header.biCompression = BI_RGB;
    header.biSizeImage = static_cast<DWORD>(targetStride * renderHeight);

    // HALFTONE statt COLORONCOLOR: der Treiber vergroessert hier meist noch ein
    // wenig -- das Zwischenbild rechnet in Geraetepunkten, aber hoechstens mit
    // 600 dpi -- und ohne Glaettung braechte das an jeder Kante Treppen aufs
    // Blatt. SetBrushOrgEx gehoert dazu: HALFTONE verschiebt sonst das Raster.
    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);

    const int written = StretchDIBits(dc, place.x, place.y, place.width, place.height, 0, 0,
                                      static_cast<int>(renderWidth),
                                      static_cast<int>(renderHeight), pixelBuffer.get(),
                                      reinterpret_cast<const BITMAPINFO*>(&header),
                                      DIB_RGB_COLORS, SRCCOPY);
    if (written == static_cast<int>(GDI_ERROR))
    {
        error = L"Der Druckertreiber hat das Bild nicht angenommen.";
        return false;
    }
    return true;
}

PrintOutcome PrintImage(HWND owner, IWICImagingFactory* wic, const PrintJob& job,
                        std::wstring& error)
{
    if (wic == nullptr || job.document == nullptr || !job.document->IsOpen())
    {
        error = L"Es ist kein Bild geoeffnet.";
        return PrintOutcome::Failed;
    }

    // Eine Animation ist eine Seite: ihre Einzelbilder sind derselbe Vorgang in
    // der Zeit, keine Blaetter. Ein GIF von zwanzig Frames auf zwanzig Seiten zu
    // werfen waere kein Dienst, sondern ein Missverstaendnis.
    const bool single = job.composed != nullptr;
    const UINT pageCount = single ? 1u : job.document->FrameCount();
    const UINT currentPage = job.currentPage < pageCount ? job.currentPage : 0;

    PRINTPAGERANGE ranges[1]{};
    ranges[0].nFromPage = currentPage + 1;
    ranges[0].nToPage = currentPage + 1;

    PRINTDLGEXW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    // PD_NOSELECTION: eine "Auswahl" gibt es hier nicht, der Betrachter kennt
    // keine Markierung. PD_USEDEVMODECOPIESANDCOLLATE ueberlaesst Kopien und
    // Sortierung dem Treiber, der das ungleich schneller kann als wir --
    // dieselbe Seite mehrfach zu drucken hiesse sonst, sie mehrfach zu dekodieren.
    dialog.Flags = PD_RETURNDC | PD_NOSELECTION | PD_USEDEVMODECOPIESANDCOLLATE;
    dialog.nMinPage = 1;
    dialog.nMaxPage = pageCount;
    dialog.nMaxPageRanges = 1;
    dialog.nPageRanges = 1;
    dialog.lpPageRanges = ranges;
    dialog.nCopies = 1;
    dialog.nStartPage = START_PAGE_GENERAL;

    if (pageCount > 1)
    {
        // Vorgewaehlt ist die gezeigte Seite, nicht das ganze Dokument. Wer bei
        // Seite 3 eines Faxes auf Drucken tippt, meint diese Seite; "Alle" ist
        // einen Klick entfernt, ein versehentlich ausgeworfener Stapel dagegen
        // nicht mehr einzusammeln.
        dialog.Flags |= PD_CURRENTPAGE;
    }
    else
    {
        dialog.Flags |= PD_NOPAGENUMS | PD_NOCURRENTPAGE;
    }

    const HRESULT hr = PrintDlgExW(&dialog);

    auto release = [&dialog]()
    {
        if (dialog.hDC != nullptr)
            DeleteDC(dialog.hDC);
        if (dialog.hDevMode != nullptr)
            GlobalFree(dialog.hDevMode);
        if (dialog.hDevNames != nullptr)
            GlobalFree(dialog.hDevNames);
    };

    if (FAILED(hr))
    {
        release();
        error = L"Der Druckdialog liess sich nicht oeffnen.\n\n" + FormatHResult(hr);
        return PrintOutcome::Failed;
    }
    if (dialog.dwResultAction != PD_RESULT_PRINT)
    {
        release();
        return PrintOutcome::Cancelled;
    }
    if (dialog.hDC == nullptr)
    {
        release();
        error = L"Es ist kein Drucker eingerichtet.";
        return PrintOutcome::Failed;
    }

    const std::vector<UINT> pages = ChosenPages(dialog, pageCount, currentPage, single);
    if (pages.empty())
    {
        release();
        return PrintOutcome::Cancelled;
    }

    // Kopien nimmt gewoehnlich der Treiber ab. Loescht er das Flag, kann er es
    // nicht -- dann wird die ganze Folge wiederholt, also sortiert ausgegeben.
    const UINT copies = ((dialog.Flags & PD_USEDEVMODECOPIESANDCOLLATE) == 0 && dialog.nCopies > 1)
                            ? dialog.nCopies
                            : 1u;

    // Der Dateiname landet in der Warteschlange. Ein Auftrag, der dort
    // "Bildanzeige" hiesse, waere bei drei wartenden nicht zu unterscheiden.
    std::wstring name = job.document->Path();
    if (const wchar_t* file = PathFindFileNameW(name.c_str()))
        name = file;

    DOCINFOW info{};
    info.cbSize = sizeof(info);
    info.lpszDocName = name.c_str();

    // Gedruckt wird im UI-Thread, und solange steht das Fenster. Der Weg in den
    // Hintergrund-Thread waere hier teuer erkauft: der Dialog davor ist ohnehin
    // modal, die Arbeit danach ist durch die Aufloesung des Geraets nach oben
    // begrenzt, und ein Auftrag, der einen Dateiwechsel ueberdauert, braeuchte
    // eine zweite Buchfuehrung ueber Zustaende, die sich unterdessen aendern.
    const HCURSOR previous = SetCursor(LoadCursorW(nullptr, IDC_WAIT));

    bool ok = StartDocW(dialog.hDC, &info) > 0;
    if (!ok)
    {
        error = L"Der Druckauftrag wurde nicht angenommen.";
    }
    else
    {
        for (UINT copy = 0; ok && copy < copies; ++copy)
        {
            for (const UINT page : pages)
            {
                ComPtr<IWICBitmapSource> source;
                if (single)
                    source = job.composed;
                else
                    source = job.document->LoadFrame(wic, page, error);
                if (!source)
                {
                    ok = false;
                    break;
                }

                if (StartPage(dialog.hDC) <= 0)
                {
                    error = L"Der Drucker hat die Seite nicht angenommen.";
                    ok = false;
                    break;
                }
                if (!PrintPageToDC(dialog.hDC, wic, source.Get(), job.rotationQuarters, nullptr,
                                   error))
                {
                    ok = false;
                    break;
                }
                if (EndPage(dialog.hDC) <= 0)
                {
                    error = L"Der Drucker hat die Seite nicht abgeschlossen.";
                    ok = false;
                    break;
                }
            }
        }

        if (ok)
        {
            ok = EndDoc(dialog.hDC) > 0;
            if (!ok)
                error = L"Der Druckauftrag wurde nicht abgeschlossen.";
        }
        else
        {
            // Ohne das bliebe ein halber Auftrag in der Warteschlange stehen und
            // braechte Papier mit halben Seiten heraus.
            AbortDoc(dialog.hDC);
        }
    }

    SetCursor(previous);
    release();
    return ok ? PrintOutcome::Printed : PrintOutcome::Failed;
}
