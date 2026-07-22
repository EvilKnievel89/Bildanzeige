#include "PrintPreview.h"

#include "ImageDocument.h"
#include "Resource.h"

#include <shlwapi.h>
#include <winspool.h>

#include <algorithm>
#include <string>

namespace
{
    constexpr wchar_t kPreviewClass[] = L"BildanzeigePrintPreview";
    constexpr wchar_t kAppTitle[] = L"Bildanzeige";

    // Feinheit, mit der das Blatt für die Ansicht gezeichnet wird. In
    // Druckauflösung zu rechnen kostete bei einem großen Scan Sekunden und
    // zeigte am Schirm keinen Punkt mehr; 150 dpi ergeben bei A4 rund
    // 1240 x 1754 Punkte und reichen für jede Fenstergröße, die auf einen
    // Schirm passt. Feste Zahl statt einer aus dem Fenster errechneten: sonst
    // müsste bei jedem Ziehen am Rahmen neu dekodiert werden.
    constexpr int kPreviewDpi = 150;

    // Maße in DIP, auf die DPI-Stufe des Fensters hochgerechnet.
    constexpr int kInitialWidth = 640;
    constexpr int kInitialHeight = 820;
    constexpr int kBarHeight = 46;
    constexpr int kButtonHeight = 26;
    constexpr int kMargin = 12;
    constexpr int kSheetMargin = 20;
    constexpr int kStepButton = 32;
    constexpr int kWideButton = 108;
    constexpr int kPageTextWidth = 96;
    constexpr int kCheckWidth = 184;
    constexpr int kGap = 4;

    constexpr int kIdPrevious = 1001;
    constexpr int kIdNext = 1002;
    constexpr int kIdPrint = 1003;
    constexpr int kIdClose = 1004;
    constexpr int kIdEnlarge = 1005;

    struct Preview
    {
        HWND hwnd = nullptr;
        HWND previous = nullptr;
        HWND next = nullptr;
        HWND enlargeBox = nullptr;
        HWND print = nullptr;
        HWND close = nullptr;
        HFONT font = nullptr;

        const PrintJob* job = nullptr;
        IWICImagingFactory* wic = nullptr;
        std::wstring printer;

        HDC printerDC = nullptr;      // Bezugsgerät der Metadatei
        HENHMETAFILE page = nullptr;  // die aufgezeichnete Seite
        std::wstring pageError;       // leer, solange die Seite steht

        UINT pageIndex = 0;
        UINT pageCount = 1;
        bool enlargeToFit = false;   // Stand des Kästchens

        int sheetWidth = 0;      // Blatt in Punkten des Geräts
        int sheetHeight = 0;
        int areaWidth = 0;       // bedruckbarer Bereich, ebenso
        int areaHeight = 0;
        int offsetX = 0;         // dessen Ecke auf dem Blatt
        int offsetY = 0;

        UINT dpi = 96;
        bool running = true;
        PrintOutcome outcome = PrintOutcome::Cancelled;
        std::wstring* error = nullptr;
    };

    int Scaled(const Preview& preview, int dip)
    {
        return MulDiv(dip, static_cast<int>(preview.dpi), 96);
    }

    std::wstring DefaultPrinter()
    {
        DWORD length = 0;
        GetDefaultPrinterW(nullptr, &length);
        if (length == 0)
            return std::wstring();

        std::wstring name(length, L'\0');
        if (!GetDefaultPrinterW(name.data(), &length))
            return std::wstring();

        name.resize(wcslen(name.c_str()));
        return name;
    }

    // Die gezeigte Seite in eine Metadatei zeichnen. Bezugsgerät ist der
    // Drucker: PrintPageToDC rechnet dadurch mit dessen wirklichen Maßen, und
    // der Rahmen der Aufzeichnung ist genau sein bedruckbarer Bereich.
    void RecordPage(Preview& preview)
    {
        if (preview.page != nullptr)
        {
            DeleteEnhMetaFile(preview.page);
            preview.page = nullptr;
        }
        preview.pageError.clear();

        ComPtr<IWICBitmapSource> source;
        if (preview.job->composed != nullptr)
            source = preview.job->composed;
        else
            source = preview.job->document->LoadFrame(preview.wic, preview.pageIndex,
                                                      preview.pageError);
        if (!source)
            return;

        const RECT frame = PrintMetafileFrame(preview.printerDC);
        HDC meta = CreateEnhMetaFileW(preview.printerDC, nullptr, &frame, nullptr);
        if (meta == nullptr)
        {
            preview.pageError = L"Die Seite ließ sich nicht aufzeichnen.";
            return;
        }

        const bool drawn = PrintPageToDC(meta, preview.wic, source.Get(),
                                         preview.job->rotationQuarters, preview.enlargeToFit,
                                         kPreviewDpi, nullptr, preview.pageError);
        HENHMETAFILE recorded = CloseEnhMetaFile(meta);
        if (!drawn || recorded == nullptr)
        {
            if (recorded != nullptr)
                DeleteEnhMetaFile(recorded);
            if (preview.pageError.empty())
                preview.pageError = L"Die Seite ließ sich nicht aufzeichnen.";
            return;
        }
        preview.page = recorded;
    }

    void UpdateTitle(Preview& preview)
    {
        std::wstring name = preview.job->document->Path();
        if (const wchar_t* file = PathFindFileNameW(name.c_str()))
            name = file;

        std::wstring title = L"Seitenansicht: " + name;
        if (preview.pageCount > 1)
        {
            title += L"  [Seite " + std::to_wstring(preview.pageIndex + 1) + L"/" +
                     std::to_wstring(preview.pageCount) + L"]";
        }
        title += L"  —  " + preview.printer;
        SetWindowTextW(preview.hwnd, title.c_str());
    }

    void UpdateButtons(Preview& preview)
    {
        if (preview.previous == nullptr || preview.next == nullptr)
            return;
        EnableWindow(preview.previous, preview.pageIndex > 0);
        EnableWindow(preview.next, preview.pageIndex + 1 < preview.pageCount);
    }

    // Wo das Blatt im Fenster liegt und wo darin der bedruckbare Bereich.
    // Beides wird gebraucht: die Aufzeichnung kennt nur den bedruckbaren
    // Bereich -- ihr Rahmen ist genau er --, gezeigt werden soll aber das ganze
    // Blatt samt dem Rand, den das Gerät nicht erreicht.
    void SheetRects(const Preview& preview, const RECT& client, RECT& sheet, RECT& printable)
    {
        sheet = RECT{ 0, 0, 0, 0 };
        printable = sheet;
        if (preview.sheetWidth <= 0 || preview.sheetHeight <= 0)
            return;

        const int margin = Scaled(preview, kSheetMargin);
        const int availableWidth = client.right - 2 * margin;
        const int availableHeight = client.bottom - Scaled(preview, kBarHeight) - 2 * margin;
        if (availableWidth <= 0 || availableHeight <= 0)
            return;

        const double scale = std::min(static_cast<double>(availableWidth) / preview.sheetWidth,
                                      static_cast<double>(availableHeight) / preview.sheetHeight);
        const int width = std::max(1, static_cast<int>(preview.sheetWidth * scale));
        const int height = std::max(1, static_cast<int>(preview.sheetHeight * scale));
        const int left = margin + (availableWidth - width) / 2;
        const int top = margin + (availableHeight - height) / 2;

        sheet = RECT{ left, top, left + width, top + height };
        printable = RECT{ left + static_cast<int>(preview.offsetX * scale),
                          top + static_cast<int>(preview.offsetY * scale),
                          left + static_cast<int>((preview.offsetX + preview.areaWidth) * scale),
                          top + static_cast<int>((preview.offsetY + preview.areaHeight) * scale) };
    }

    void PaintPreview(Preview& preview, HDC target, const RECT& client)
    {
        // Doppelt gepuffert: das Blatt, die Aufzeichnung und der Rahmen sind
        // drei Durchgänge, und ohne Zwischenbild flackerte es bei jeder
        // Größenänderung.
        HDC buffer = CreateCompatibleDC(target);
        HBITMAP surface = CreateCompatibleBitmap(target, client.right, client.bottom);
        HGDIOBJ previousBitmap = SelectObject(buffer, surface);

        HBRUSH backdrop = CreateSolidBrush(RGB(0x3C, 0x3C, 0x3C));
        FillRect(buffer, &client, backdrop);
        DeleteObject(backdrop);

        const RECT bar = { 0, client.bottom - Scaled(preview, kBarHeight), client.right,
                           client.bottom };
        FillRect(buffer, &bar, GetSysColorBrush(COLOR_BTNFACE));

        RECT sheet{};
        RECT printable{};
        SheetRects(preview, client, sheet, printable);
        if (sheet.right > sheet.left)
        {
            FillRect(buffer, &sheet, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            if (preview.page != nullptr)
            {
                // Auf den Platz des bedruckbaren Bereichs gespielt, nicht auf
                // das ganze Blatt: der unbedruckbare Rand bleibt weiß stehen,
                // und genau so kommt es aus dem Gerät.
                PlayEnhMetaFile(buffer, preview.page, &printable);
            }
            FrameRect(buffer, &sheet, GetSysColorBrush(COLOR_WINDOWFRAME));
        }

        HGDIOBJ previousFont = SelectObject(buffer, preview.font);
        SetBkMode(buffer, TRANSPARENT);

        if (!preview.pageError.empty())
        {
            SetTextColor(buffer, RGB(0xE0, 0xE0, 0xE0));
            RECT text = { Scaled(preview, kSheetMargin), (bar.top - client.top) / 3,
                          client.right - Scaled(preview, kSheetMargin), bar.top };
            DrawTextW(buffer, preview.pageError.c_str(), -1, &text, DT_CENTER | DT_WORDBREAK);
        }

        if (preview.pageCount > 1)
        {
            SetTextColor(buffer, GetSysColor(COLOR_BTNTEXT));
            const std::wstring text = L"Seite " + std::to_wstring(preview.pageIndex + 1) + L" / " +
                                      std::to_wstring(preview.pageCount);
            RECT where = { Scaled(preview, kMargin + kStepButton + kGap), bar.top,
                           Scaled(preview, kMargin + kStepButton + kGap + kPageTextWidth),
                           bar.bottom };
            DrawTextW(buffer, text.c_str(), -1, &where, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(buffer, previousFont);

        BitBlt(target, 0, 0, client.right, client.bottom, buffer, 0, 0, SRCCOPY);
        SelectObject(buffer, previousBitmap);
        DeleteObject(surface);
        DeleteDC(buffer);
    }

    void LayoutBar(Preview& preview)
    {
        if (preview.previous == nullptr || preview.close == nullptr)
            return;   // WM_SIZE trifft schon ein, bevor die Knöpfe stehen.

        RECT client{};
        GetClientRect(preview.hwnd, &client);
        const int barHeight = Scaled(preview, kBarHeight);
        const int height = Scaled(preview, kButtonHeight);
        const int y = client.bottom - barHeight + (barHeight - height) / 2;
        const int margin = Scaled(preview, kMargin);
        const int step = Scaled(preview, kStepButton);
        const int wide = Scaled(preview, kWideButton);
        const int gap = Scaled(preview, kGap);

        MoveWindow(preview.previous, margin, y, step, height, TRUE);
        MoveWindow(preview.next, margin + step + 2 * gap + Scaled(preview, kPageTextWidth), y, step,
                   height, TRUE);

        const int printLeft = client.right - margin - 2 * wide - gap;
        MoveWindow(preview.print, printLeft, y, wide, height, TRUE);
        MoveWindow(preview.close, client.right - margin - wide, y, wide, height, TRUE);

        // Das Kästchen sitzt hinter den Schrittknöpfen -- und bei einer
        // einzelnen Seite, wo die fortbleiben, an deren Stelle. Was zwischen
        // ihm und "Drucken …" nicht mehr an Platz ist, nimmt es sich auch
        // nicht: bei einem schmal gezogenen Fenster bleibt es lieber
        // abgeschnitten, als unter den Knöpfen zu liegen.
        if (preview.enlargeBox != nullptr)
        {
            const int left =
                margin + (preview.pageCount > 1
                              ? 2 * step + 4 * gap + Scaled(preview, kPageTextWidth)
                              : 0);
            const int room = std::min(Scaled(preview, kCheckWidth), printLeft - gap - left);
            MoveWindow(preview.enlargeBox, left, y, std::max(0, room), height, TRUE);
        }
    }

    void GoToPage(Preview& preview, int delta)
    {
        const int target = static_cast<int>(preview.pageIndex) + delta;
        if (preview.pageCount <= 1 || target < 0 || target >= static_cast<int>(preview.pageCount))
            return;

        preview.pageIndex = static_cast<UINT>(target);

        // Eine neue Seite heißt: neu dekodieren. Bei einem großen Scan dauert
        // das, und ohne die Sanduhr sähe es aus, als sei der Klick verloren.
        const HCURSOR busy = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
        RecordPage(preview);
        SetCursor(busy);

        UpdateTitle(preview);
        UpdateButtons(preview);
        InvalidateRect(preview.hwnd, nullptr, FALSE);
    }

    void OnCommand(Preview& preview, int id)
    {
        switch (id)
        {
        case kIdClose:
            preview.running = false;
            break;

        case kIdPrevious:
            GoToPage(preview, -1);
            break;

        case kIdNext:
            GoToPage(preview, +1);
            break;

        case kIdEnlarge:
        {
            // Der Knopf schaltet sich selbst um (BS_AUTOCHECKBOX); hier ist nur
            // abzulesen, wie er jetzt steht, und die Seite neu aufzuzeichnen --
            // dieselbe Arbeit wie bei einem Seitenwechsel, also auch dieselbe
            // Sanduhr.
            preview.enlargeToFit =
                SendMessageW(preview.enlargeBox, BM_GETCHECK, 0, 0) == BST_CHECKED;

            const HCURSOR busy = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
            RecordPage(preview);
            SetCursor(busy);
            InvalidateRect(preview.hwnd, nullptr, FALSE);
            break;
        }

        case kIdPrint:
        {
            // Besitzer des Druckdialogs ist die Ansicht, nicht das Hauptfenster:
            // das ist gesperrt, und ein Dialog davor stünde ohne Bezug da.
            //
            // Die Seite, die gerade gezeigt wird, geht als die aktuelle in den
            // Auftrag -- wer bis Seite 3 geblättert hat, meint diese.
            PrintJob started = *preview.job;
            started.currentPage = preview.pageIndex;
            started.enlargeToFit = preview.enlargeToFit;
            preview.outcome = PrintImage(preview.hwnd, preview.wic, started, *preview.error);
            if (preview.outcome != PrintOutcome::Cancelled)
                preview.running = false;
            break;
        }

        default:
            break;
        }
    }

    LRESULT CALLBACK PreviewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_NCCREATE)
        {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        auto* preview = reinterpret_cast<Preview*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (preview == nullptr)
            return DefWindowProcW(hwnd, msg, wParam, lParam);

        switch (msg)
        {
        case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);
            PaintPreview(*preview, dc, client);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;   // Verhindert Flackern; PaintPreview füllt die Fläche.

        case WM_SIZE:
            LayoutBar(*preview);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_COMMAND:
            // Ein Knopf *sendet* seine Meldung an das Elternfenster; in der
            // Nachrichtenschleife käme sie nie an.
            OnCommand(*preview, LOWORD(wParam));
            return 0;

        case WM_CTLCOLORBTN:
        case WM_CTLCOLORSTATIC:
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));

        case WM_CLOSE:
            preview->running = false;
            return 0;

        default:
            break;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    bool OnKey(Preview& preview, WPARAM key)
    {
        switch (key)
        {
        case VK_ESCAPE:
            preview.running = false;
            return true;

        case VK_NEXT:
            GoToPage(preview, +1);
            return true;

        case VK_PRIOR:
            GoToPage(preview, -1);
            return true;

        case VK_HOME:
            GoToPage(preview, -static_cast<int>(preview.pageIndex));
            return true;

        case VK_END:
            GoToPage(preview, static_cast<int>(preview.pageCount - 1 - preview.pageIndex));
            return true;

        case VK_RETURN:
        {
            // Ohne Dialogvorlage gibt es keinen Standardknopf, den die
            // Eingabetaste von selbst fände -- sie wird deshalb von Hand auf
            // den Knopf gelegt, der gerade den Fokus hat.
            const HWND focus = GetFocus();
            if (focus == preview.enlargeBox)
            {
                // Ein Kästchen hat keinen Befehl, den die Eingabetaste
                // auslösen könnte; sie schaltet es deshalb um, wie es die
                // Leertaste ohnehin tut.
                SendMessageW(preview.enlargeBox, BM_CLICK, 0, 0);
                return true;
            }

            int id = kIdPrint;
            if (focus == preview.close)
                id = kIdClose;
            else if (focus == preview.previous)
                id = kIdPrevious;
            else if (focus == preview.next)
                id = kIdNext;
            OnCommand(preview, id);
            return true;
        }

        default:
            return false;
        }
    }
}

PrintOutcome ShowPrintPreview(HWND owner, IWICImagingFactory* wic, const PrintJob& job,
                              std::wstring& error)
{
    if (wic == nullptr || job.document == nullptr || !job.document->IsOpen())
    {
        error = L"Es ist kein Bild geöffnet.";
        return PrintOutcome::Failed;
    }

    Preview preview;
    preview.job = &job;
    preview.wic = wic;
    preview.error = &error;
    preview.printer = DefaultPrinter();
    preview.pageCount = job.composed != nullptr ? 1u : job.document->FrameCount();
    preview.pageIndex = job.currentPage < preview.pageCount ? job.currentPage : 0;
    preview.enlargeToFit = job.enlargeToFit;

    if (preview.printer.empty())
    {
        error = L"Es ist kein Drucker eingerichtet.";
        return PrintOutcome::Failed;
    }

    preview.printerDC = CreateDCW(nullptr, preview.printer.c_str(), nullptr, nullptr);
    if (preview.printerDC == nullptr)
    {
        error = L"Der Drucker \"" + preview.printer + L"\" liefert keinen Gerätekontext.";
        return PrintOutcome::Failed;
    }

    preview.areaWidth = GetDeviceCaps(preview.printerDC, HORZRES);
    preview.areaHeight = GetDeviceCaps(preview.printerDC, VERTRES);
    preview.sheetWidth = GetDeviceCaps(preview.printerDC, PHYSICALWIDTH);
    preview.sheetHeight = GetDeviceCaps(preview.printerDC, PHYSICALHEIGHT);
    preview.offsetX = GetDeviceCaps(preview.printerDC, PHYSICALOFFSETX);
    preview.offsetY = GetDeviceCaps(preview.printerDC, PHYSICALOFFSETY);
    if (preview.sheetWidth <= 0 || preview.sheetHeight <= 0)
    {
        // Treiber ohne Angaben zum Blatt -- dieselbe Ersatzannahme wie beim
        // Drucken: dann ist der bedruckbare Bereich das ganze Blatt.
        preview.sheetWidth = preview.areaWidth;
        preview.sheetHeight = preview.areaHeight;
        preview.offsetX = 0;
        preview.offsetY = 0;
    }

    const HINSTANCE instance =
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE));

    WNDCLASSEXW existing{};
    existing.cbSize = sizeof(existing);
    if (!GetClassInfoExW(instance, kPreviewClass, &existing))
    {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &PreviewProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                                 GetSystemMetrics(SM_CXICON),
                                                 GetSystemMetrics(SM_CYICON), LR_SHARED));
        wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                                   GetSystemMetrics(SM_CXSMICON),
                                                   GetSystemMetrics(SM_CYSMICON), LR_SHARED));
        wc.lpszClassName = kPreviewClass;
        if (RegisterClassExW(&wc) == 0)
        {
            DeleteDC(preview.printerDC);
            error = L"Die Seitenansicht ließ sich nicht anlegen.";
            return PrintOutcome::Failed;
        }
    }

    preview.dpi = GetDpiForWindow(owner);
    if (preview.dpi == 0)
        preview.dpi = 96;

    preview.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kPreviewClass, kAppTitle,
                                   WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                   CW_USEDEFAULT, CW_USEDEFAULT, owner, nullptr, instance,
                                   &preview);
    if (preview.hwnd == nullptr)
    {
        DeleteDC(preview.printerDC);
        error = L"Die Seitenansicht ließ sich nicht anlegen.";
        return PrintOutcome::Failed;
    }

    // Größe erst jetzt, aus demselben Grund wie beim Hauptfenster: fest in
    // Pixeln angegeben wäre das Fenster bei 150 % nur zwei Drittel so groß
    // wie gemeint. Gesetzt wird es mittig über dem Hauptfenster.
    RECT desired = { 0, 0, Scaled(preview, kInitialWidth), Scaled(preview, kInitialHeight) };
    if (AdjustWindowRectExForDpi(&desired, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_DLGMODALFRAME,
                                 preview.dpi))
    {
        RECT ownerRect{};
        GetWindowRect(owner, &ownerRect);
        const int width = desired.right - desired.left;
        const int height = desired.bottom - desired.top;
        SetWindowPos(preview.hwnd, nullptr,
                     ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2,
                     ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2, width,
                     height, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    preview.font = CreateFontW(-MulDiv(9, static_cast<int>(preview.dpi), 72), 0, 0, 0, FW_NORMAL,
                               FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");

    struct ButtonSpec
    {
        const wchar_t* text;
        int id;
        HWND* target;
        DWORD extra;
    };
    // Die Beschriftungen stehen als Zeichen da, nicht als \x-Folgen: eine
    // Hexfolge im Zeichenkettenliteral frisst so viele Ziffern, wie sie
    // bekommen kann -- aus "\x00DFen" würde ein einzelnes Zeichen 0x0DFE und
    // aus "Schließen" ein Kästchen. Übersetzt wird ohnehin mit /utf-8.
    const ButtonSpec specs[] = {
        { L"‹", kIdPrevious, &preview.previous, 0 },
        { L"›", kIdNext, &preview.next, 0 },
        { L"Kleine Bilder ver&größern", kIdEnlarge, &preview.enlargeBox, BS_AUTOCHECKBOX },
        { L"&Drucken …", kIdPrint, &preview.print, BS_DEFPUSHBUTTON },
        { L"S&chließen", kIdClose, &preview.close, 0 },
    };
    for (const ButtonSpec& spec : specs)
    {
        *spec.target = CreateWindowExW(
            0, L"BUTTON", spec.text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | spec.extra, 0, 0, 0, 0,
            preview.hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(spec.id)), instance,
            nullptr);
        if (*spec.target != nullptr)
            SendMessageW(*spec.target, WM_SETFONT, reinterpret_cast<WPARAM>(preview.font), TRUE);
    }

    if (preview.enlargeBox != nullptr && preview.enlargeToFit)
        SendMessageW(preview.enlargeBox, BM_SETCHECK, BST_CHECKED, 0);

    // Bei einer einzelnen Seite gibt es nichts zu blättern; die Schrittknöpfe
    // bleiben fort, statt dauerhaft grau dazustehen -- dieselbe Regel wie in
    // der Icon-Leiste des Hauptfensters.
    if (preview.pageCount <= 1)
    {
        ShowWindow(preview.previous, SW_HIDE);
        ShowWindow(preview.next, SW_HIDE);
    }

    LayoutBar(preview);
    RecordPage(preview);
    UpdateTitle(preview);
    UpdateButtons(preview);

    // Von Hand modal: das Hauptfenster wird gesperrt, und die Schleife läuft,
    // bis die Ansicht geschlossen ist. Ein Dialog aus Ressourcen wäre der
    // übliche Weg, brauchte aber eine Vorlagendatei für ein Fenster, das
    // seine Fläche ohnehin selbst zeichnet.
    EnableWindow(owner, FALSE);
    ShowWindow(preview.hwnd, SW_SHOW);
    SetFocus(preview.print);

    MSG msg{};
    while (preview.running && GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        if (msg.message == WM_KEYDOWN && OnKey(preview, msg.wParam))
            continue;

        // IsDialogMessage liefert das Blättern mit der Tabulatortaste und die
        // Kurztasten der Knopfbeschriftungen (Alt+D, Alt+C).
        if (!IsDialogMessageW(preview.hwnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    DestroyWindow(preview.hwnd);

    if (preview.page != nullptr)
        DeleteEnhMetaFile(preview.page);
    if (preview.font != nullptr)
        DeleteObject(preview.font);
    DeleteDC(preview.printerDC);

    // Eine Seite, die sich nicht zeichnen ließ, ist eine Meldung wert -- aber
    // nur, wenn nicht ohnehin gedruckt wurde.
    if (preview.outcome == PrintOutcome::Cancelled && !preview.pageError.empty())
    {
        error = preview.pageError;
        return PrintOutcome::Failed;
    }
    return preview.outcome;
}
