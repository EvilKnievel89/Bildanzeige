#include "SettingsDialog.h"

#include "PrintDialogSetting.h"
#include "Resource.h"

#include <algorithm>
#include <string>

namespace
{
    constexpr wchar_t kDialogClass[] = L"BildanzeigeSettings";
    constexpr wchar_t kAppTitle[] = L"Bildanzeige";

    constexpr int kIdOk = 1301;
    constexpr int kIdCancel = 1302;
    constexpr int kIdLegacyPrint = 1303;

    // Maße in DIP, auf die DPI-Stufe des Fensters hochgerechnet -- wie im
    // Fenster für die Dateizuordnungen, damit beide gleich aussehen.
    constexpr int kWidth = 460;
    constexpr int kMargin = 16;
    constexpr int kGap = 12;
    constexpr int kRowHeight = 22;
    constexpr int kButtonHeight = 28;
    constexpr int kButtonGap = 8;
    constexpr int kOkWidth = 100;
    constexpr int kCancelWidth = 100;

    struct Dialog
    {
        HWND hwnd = nullptr;
        HWND header = nullptr;
        HWND legacyPrint = nullptr;
        HWND note = nullptr;
        HWND ok = nullptr;
        HWND cancel = nullptr;
        HFONT font = nullptr;

        RegistryScope scope = RegistryScope::CurrentUser;
        UINT dpi = 96;
        int headerHeight = 0;   // in Pixeln, gemessen
        int noteHeight = 0;
        int lineHeight = 0;

        // Der Stand, wie er beim Öffnen in der Registrierung stand. Geschrieben
        // wird nur, was sich davon unterscheidet -- so bleibt ein Zweig
        // unangetastet, den jemand bloß angesehen hat.
        bool legacyWasOn = false;

        bool running = true;
    };

    int Scaled(const Dialog& dialog, int dip)
    {
        return MulDiv(dip, static_cast<int>(dialog.dpi), 96);
    }

    // Wie hoch der Text in dieser Schrift wird, wenn er in der gegebenen Breite
    // umbricht. Die Texte unten sind je nach Zweig und Lage verschieden lang,
    // und ein fest angenommener Platz stünde bei 150 % zu knapp.
    int TextHeight(HFONT font, const std::wstring& text, int width)
    {
        const HDC dc = GetDC(nullptr);
        if (dc == nullptr)
            return 0;

        const HGDIOBJ previous = SelectObject(dc, font);
        RECT rect = { 0, 0, width, 0 };
        DrawTextW(dc, text.c_str(), -1, &rect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, previous);
        ReleaseDC(nullptr, dc);
        return rect.bottom;
    }

    std::wstring HeaderText(RegistryScope scope)
    {
        if (scope == RegistryScope::LocalMachine)
        {
            // Der Zweig des Benutzers geht dem des Rechners vor. Wer das nicht
            // liest, stellt hier um und sucht die Wirkung später vergebens.
            std::wstring text =
                L"Die Einstellung geht nach HKEY_LOCAL_MACHINE: sie gilt für alle Benutzer "
                L"dieses Rechners.";
            if (HasLegacyPrintDialog(RegistryScope::CurrentUser))
            {
                text += L"\n\nFür den angemeldeten Benutzer steht daneben ein eigener Wert. Der "
                        L"geht dem hier Geschriebenen vor und lässt sich nur in einem gewöhnlich "
                        L"gestarteten Lauf ändern.";
            }
            return text;
        }

        std::wstring text =
            L"Die Einstellung geht nach HKEY_CURRENT_USER: sie gilt nur für den angemeldeten "
            L"Benutzer.";

        // Erhöht gestartet und trotzdem der Benutzerzweig: das hat einen Grund,
        // und ohne ihn sähe es nach einem Fehler aus.
        if (ProcessScope() == RegistryScope::LocalMachine)
        {
            text += L"\n\nFür alle Benutzer wird nur dort geschrieben, wo der Wert schon steht. "
                    L"Unter HKEY_LOCAL_MACHINE steht auf diesem Rechner keiner; angelegt wird er "
                    L"von hier aus nicht.";
        }
        return text;
    }

    // Die Fußzeile. Sie steht hier für sich, weil ihre Höhe vor dem Anlegen des
    // Fensters feststehen muss.
    std::wstring NoteText()
    {
        return L"Windows 11 zeigt Win32-Anwendungen sonst seinen neuen Druckdialog. Die "
               L"Umstellung wirkt beim nächsten Druckdialog — comdlg32.dll liest den Wert bei "
               L"jedem Aufruf neu, ein Neustart von Windows, des Explorers oder der Anwendung "
               L"ist dafür nicht nötig.\n"
               L"Sie gilt für alle Anwendungen, nicht nur für die Bildanzeige.";
    }

    void Layout(Dialog& dialog)
    {
        RECT client{};
        GetClientRect(dialog.hwnd, &client);

        const int margin = Scaled(dialog, kMargin);
        const int gap = Scaled(dialog, kGap);
        const int width = client.right - 2 * margin;
        int y = margin;

        MoveWindow(dialog.header, margin, y, width, dialog.headerHeight, TRUE);
        y += dialog.headerHeight + gap;

        const int rowHeight = std::max(Scaled(dialog, kRowHeight), dialog.lineHeight);
        MoveWindow(dialog.legacyPrint, margin, y, width, rowHeight, TRUE);
        y += rowHeight + gap;

        MoveWindow(dialog.note, margin, y, width, dialog.noteHeight, TRUE);

        // Die Knöpfe stehen am unteren Rand, von rechts nach links aufgereiht.
        const int buttonHeight = Scaled(dialog, kButtonHeight);
        const int buttonGap = Scaled(dialog, kButtonGap);
        const int buttonY = client.bottom - margin - buttonHeight;

        int right = client.right - margin;
        const struct
        {
            HWND window;
            int width;
        } buttons[] = {
            { dialog.cancel, Scaled(dialog, kCancelWidth) },
            { dialog.ok, Scaled(dialog, kOkWidth) },
        };
        for (const auto& button : buttons)
        {
            MoveWindow(button.window, right - button.width, buttonY, button.width, buttonHeight,
                       TRUE);
            right -= button.width + buttonGap;
        }
    }

    // Liefert true, wenn das Fenster zugehen darf.
    bool OnOk(Dialog& dialog)
    {
        const bool wanted =
            SendMessageW(dialog.legacyPrint, BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (wanted == dialog.legacyWasOn)
            return true;

        std::wstring error;
        if (!SetLegacyPrintDialog(dialog.scope, wanted, error))
        {
            // Offen bleiben: geschlossen sähe es aus, als wäre die Umstellung
            // angekommen, und der Haken stünde beim nächsten Öffnen wieder da,
            // wo er vorher war.
            MessageBoxW(dialog.hwnd, error.c_str(), kAppTitle, MB_ICONWARNING | MB_OK);
            return false;
        }

        dialog.legacyWasOn = wanted;
        return true;
    }

    void OnCommand(Dialog& dialog, int id)
    {
        switch (id)
        {
        case kIdOk:
            if (OnOk(dialog))
                dialog.running = false;
            break;

        case kIdCancel:
            dialog.running = false;
            break;

        default:
            break;
        }
    }

    LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_NCCREATE)
        {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        auto* dialog = reinterpret_cast<Dialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (dialog == nullptr)
            return DefWindowProcW(hwnd, msg, wParam, lParam);

        switch (msg)
        {
        case WM_COMMAND:
            OnCommand(*dialog, LOWORD(wParam));
            return 0;

        case WM_CTLCOLORBTN:
        case WM_CTLCOLORSTATIC:
        {
            // Der Text wird mit der Hintergrundfarbe des Gerätekontexts
            // unterlegt. Ohne diese beiden Zeilen stünden die Beschriftungen
            // in einem weißen Kasten auf grauem Grund.
            const HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
            SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
        }

        case WM_CLOSE:
            // Das Kreuz in der Titelleiste ist ein Abbrechen, kein OK.
            dialog->running = false;
            return 0;

        default:
            break;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    bool OnKey(Dialog& dialog, WPARAM key)
    {
        if (key == VK_ESCAPE)
        {
            dialog.running = false;
            return true;
        }

        if (key != VK_RETURN)
            return false;

        // Ohne Dialogvorlage gibt es keinen Standardknopf, den die Eingabetaste
        // von selbst fände. Auf dem Kästchen schaltet sie um, wie es die
        // Leertaste ohnehin tut; sonst gilt sie dem Knopf mit dem Fokus, und
        // hat keiner ihn, dem OK.
        const HWND focus = GetFocus();
        if (focus == dialog.legacyPrint)
        {
            SendMessageW(dialog.legacyPrint, BM_CLICK, 0, 0);
            return true;
        }

        OnCommand(dialog, focus == dialog.cancel ? kIdCancel : kIdOk);
        return true;
    }
}

void ShowSettingsDialog(HWND owner)
{
    const HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE));

    WNDCLASSEXW existing{};
    existing.cbSize = sizeof(existing);
    if (!GetClassInfoExW(instance, kDialogClass, &existing))
    {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &DialogProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        // Wie das Fenster für die Dateizuordnungen: hier stehen die
        // gewöhnlichen Bedienelemente von Windows, und die erwarten deren
        // Grundfarbe, statt dass die Fläche selbst gezeichnet würde.
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                                 GetSystemMetrics(SM_CXICON),
                                                 GetSystemMetrics(SM_CYICON), LR_SHARED));
        wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                                   GetSystemMetrics(SM_CXSMICON),
                                                   GetSystemMetrics(SM_CYSMICON), LR_SHARED));
        wc.lpszClassName = kDialogClass;
        if (RegisterClassExW(&wc) == 0)
            return;
    }

    Dialog dialog;
    dialog.scope = PrintDialogScope();
    dialog.dpi = GetDpiForWindow(owner);
    if (dialog.dpi == 0)
        dialog.dpi = 96;

    // Der Stand kommt bei jedem Öffnen frisch aus der Registrierung, nicht aus
    // einem gemerkten Wert: dort kann seit dem letzten Mal jemand anders
    // gewesen sein -- der Registrierungs-Editor, eine Richtlinie, ein zweiter
    // Lauf der Bildanzeige.
    dialog.legacyWasOn = IsLegacyPrintDialogEnabled(dialog.scope);

    dialog.font = CreateFontW(-MulDiv(9, static_cast<int>(dialog.dpi), 72), 0, 0, 0, FW_NORMAL,
                              FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
    if (dialog.font == nullptr)
        return;

    // Erst messen, dann das Fenster anlegen: seine Höhe ergibt sich aus den
    // Texten und der Schrift, nicht umgekehrt.
    const int margin = Scaled(dialog, kMargin);
    const int gap = Scaled(dialog, kGap);
    const int clientWidth = Scaled(dialog, kWidth);
    const int textWidth = clientWidth - 2 * margin;

    const std::wstring header = HeaderText(dialog.scope);
    const std::wstring note = NoteText();
    dialog.headerHeight = TextHeight(dialog.font, header, textWidth);
    dialog.noteHeight = TextHeight(dialog.font, note, textWidth);
    dialog.lineHeight = TextHeight(dialog.font, L"Ag", textWidth);

    const int rowHeight = std::max(Scaled(dialog, kRowHeight), dialog.lineHeight);
    const int clientHeight = margin + dialog.headerHeight + gap + rowHeight + gap +
                             dialog.noteHeight + gap + Scaled(dialog, kButtonHeight) + margin;

    // Fester Rahmen ohne Größenzug: an diesem Fenster gibt es nichts zu ziehen.
    constexpr DWORD style = WS_CAPTION | WS_SYSMENU;
    dialog.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kDialogClass, L"Einstellungen", style,
                                  CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, owner,
                                  nullptr, instance, &dialog);
    if (dialog.hwnd == nullptr)
    {
        DeleteObject(dialog.font);
        return;
    }

    RECT desired = { 0, 0, clientWidth, clientHeight };
    if (AdjustWindowRectExForDpi(&desired, style, FALSE, WS_EX_DLGMODALFRAME, dialog.dpi))
    {
        RECT ownerRect{};
        GetWindowRect(owner, &ownerRect);
        const int width = desired.right - desired.left;
        const int height = desired.bottom - desired.top;
        SetWindowPos(dialog.hwnd, nullptr,
                     ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2,
                     ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2, width,
                     height, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    dialog.header = CreateWindowExW(0, L"STATIC", header.c_str(), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
                                    dialog.hwnd, nullptr, instance, nullptr);
    dialog.legacyPrint = CreateWindowExW(
        0, L"BUTTON", L"&Klassischen Windows-Druckdialog verwenden",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, dialog.hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdLegacyPrint)), instance, nullptr);
    dialog.note = CreateWindowExW(0, L"STATIC", note.c_str(), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
                                  dialog.hwnd, nullptr, instance, nullptr);

    struct ButtonSpec
    {
        const wchar_t* text;
        int id;
        HWND* target;
        DWORD extra;
    };
    const ButtonSpec specs[] = {
        { L"OK", kIdOk, &dialog.ok, BS_DEFPUSHBUTTON },
        { L"Abbrechen", kIdCancel, &dialog.cancel, 0 },
    };
    for (const ButtonSpec& spec : specs)
    {
        *spec.target = CreateWindowExW(0, L"BUTTON", spec.text,
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | spec.extra, 0, 0, 0, 0,
                                       dialog.hwnd,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(spec.id)),
                                       instance, nullptr);
    }

    // Ein Fenster, das nicht aus einer Dialogvorlage stammt, bekommt für seine
    // Bedienelemente die alte Systemschrift zugeteilt. Ohne diese Schleife
    // stünde das Fenster in Segoe UI und sein Inhalt in einer Schrift von 1995.
    for (HWND child = GetWindow(dialog.hwnd, GW_CHILD); child != nullptr;
         child = GetWindow(child, GW_HWNDNEXT))
    {
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(dialog.font), TRUE);
    }

    Layout(dialog);
    SendMessageW(dialog.legacyPrint, BM_SETCHECK, dialog.legacyWasOn ? BST_CHECKED : BST_UNCHECKED,
                 0);

    // Von Hand modal, wie die Seitenansicht: das Hauptfenster wird gesperrt,
    // und die Schleife läuft, bis das Fenster geschlossen ist.
    EnableWindow(owner, FALSE);
    ShowWindow(dialog.hwnd, SW_SHOW);
    SetFocus(dialog.legacyPrint);

    MSG msg{};
    while (dialog.running && GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        if (msg.message == WM_KEYDOWN && OnKey(dialog, msg.wParam))
            continue;

        // IsDialogMessage liefert das Blättern mit der Tabulatortaste, die
        // Leertaste auf dem Kästchen und die Kurztasten der Beschriftungen.
        if (!IsDialogMessageW(dialog.hwnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    DestroyWindow(dialog.hwnd);
    DeleteObject(dialog.font);
}
