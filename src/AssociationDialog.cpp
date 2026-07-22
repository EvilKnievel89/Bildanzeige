#include "AssociationDialog.h"

#include "FileAssociation.h"
#include "Resource.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    constexpr wchar_t kDialogClass[] = L"BildanzeigeAssociations";
    constexpr wchar_t kAppTitle[] = L"Bildanzeige";

    constexpr int kIdApply = 1101;
    constexpr int kIdDefaults = 1102;
    constexpr int kIdClose = 1103;

    // Darüber liegt je Endung eine Kennung. Sie sind nur zu unterscheiden, zu
    // tun ist bei einem Kästchen nichts: BS_AUTOCHECKBOX schaltet sich selbst.
    constexpr int kIdFirstBox = 1200;

    // Maße in DIP, auf die DPI-Stufe des Fensters hochgerechnet.
    constexpr int kWidth = 460;
    constexpr int kMargin = 16;
    constexpr int kGap = 12;
    constexpr int kRowHeight = 22;
    constexpr int kButtonHeight = 28;
    constexpr int kButtonGap = 8;
    constexpr int kApplyWidth = 112;
    constexpr int kDefaultsWidth = 176;
    constexpr int kCloseWidth = 100;

    struct Dialog
    {
        HWND hwnd = nullptr;
        HWND header = nullptr;
        HWND status = nullptr;
        HWND apply = nullptr;
        HWND defaults = nullptr;
        HWND close = nullptr;
        HFONT font = nullptr;
        std::vector<HWND> boxes;

        RegistryScope scope = RegistryScope::CurrentUser;
        UINT dpi = 96;
        int headerHeight = 0;    // in Pixeln, gemessen
        int statusHeight = 0;
        int lineHeight = 0;

        // Erst wenn alle Bedienelemente stehen, darf sie jemand anfassen --
        // WM_ACTIVATE kommt sonst in ein halb aufgebautes Fenster.
        bool ready = false;
        bool running = true;
    };

    int Scaled(const Dialog& dialog, int dip)
    {
        return MulDiv(dip, static_cast<int>(dialog.dpi), 96);
    }

    // Wie hoch der Text in dieser Schrift wird, wenn er in der gegebenen Breite
    // umbricht. Der Kopftext ist je nach Zweig und Lage verschieden lang, und
    // ein fest angenommener Platz dafür stünde bei 150 % oder in einer anderen
    // Schriftgröße zu knapp.
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
        // Der jeweils andere Zweig ist von hier aus nicht zu ändern, wirkt aber
        // weiter. Wer ihn nicht erwähnt bekommt, sucht den Grund später
        // vergebens in dem, den er vor sich hat.
        const RegistryScope other = scope == RegistryScope::LocalMachine
                                        ? RegistryScope::CurrentUser
                                        : RegistryScope::LocalMachine;
        bool otherHasEntries = false;
        for (const FileType& type : FileTypes())
        {
            if (IsRegistered(other, type.extension))
            {
                otherHasEntries = true;
                break;
            }
        }

        if (scope == RegistryScope::LocalMachine)
        {
            std::wstring text =
                L"Die Einträge gehen nach HKEY_LOCAL_MACHINE: sie gelten für alle Benutzer "
                L"dieses Rechners.";

            // Und zwar für alle außer den, der gerade davorsitzt, sofern er
            // eigene Einträge hat: der Zweig des Benutzers geht dem des
            // Rechners vor.
            if (otherHasEntries)
            {
                text += L"\n\nFür den angemeldeten Benutzer ist daneben etwas eingetragen. Das "
                        L"geht dem hier Geschriebenen vor und lässt sich nur in einem gewöhnlich "
                        L"gestarteten Lauf ändern.";
            }
            return text;
        }

        std::wstring text =
            L"Die Einträge gehen nach HKEY_CURRENT_USER: sie gelten nur für den angemeldeten "
            L"Benutzer. Für alle Benutzer die Bildanzeige als Administrator starten.";

        if (otherHasEntries)
        {
            text += L"\n\nFür alle Benutzer ist bereits etwas eingetragen. Das lässt sich nur "
                    L"mit erhöhten Rechten ändern.";
        }
        return text;
    }

    // Die Fußzeile. Sie steht hier für sich, weil ihre Höhe vor dem Anlegen des
    // Fensters feststehen muss: gemessen wird der längste Fall, angezeigt der
    // jeweilige. Ein fester Platz von zwei Zeilen ginge nicht -- schon bei
    // 125 % bräche der Satz um und stünde zur Hälfte außerhalb.
    // Der Zweig steht mit in der Zeile, obwohl er schon im Kopf genannt ist.
    // Er muss es: hat der andere Zweig Einträge, stünde sonst "nichts
    // eingetragen" neben einer Liste voller "(Standard)"-Vermerke, und beides
    // wäre wahr, ohne dass man sähe, wovon die Rede ist.
    std::wstring StatusText(RegistryScope scope, size_t registered, bool anyDefault)
    {
        const std::wstring zweig = ScopeKeyName(scope);

        std::wstring text;
        if (registered == 0)
            text = L"In " + zweig + L" ist bisher nichts eingetragen.";
        else if (registered == 1)
            text = L"In " + zweig + L" eingetragen: eine Endung.";
        else
            text = L"In " + zweig + L" eingetragen: " + std::to_wstring(registered) + L" Endungen.";

        text += L"\n";
        text += anyDefault
                    ? L"(Standard) heißt: diese Endung öffnet Windows derzeit mit der Bildanzeige."
                    : L"Zum Standard wird die Anwendung erst in den Windows-Einstellungen; "
                      L"der Knopf führt hin.";
        return text;
    }

    // Fehlt die Erweiterung, verdrängt dieser Vermerk den anderen: dass die
    // Anwendung als Standard eingetragen ist, hilft bei einem Format nicht
    // weiter, das sie auf diesem Rechner gar nicht öffnen kann.
    std::wstring LabelFor(const FileType& type, bool isDefault, bool missing)
    {
        std::wstring label = std::wstring(type.extension) + L" — " + type.name;
        if (missing)
            label += L"   (Erweiterung fehlt)";
        else if (isDefault)
            label += L"   (Standard)";
        return label;
    }

    // Liest den Stand aus der Registrierung und trägt ihn ins Fenster:
    // Beschriftungen, Fußzeile, Knopfzustand. Liefert, wie viele Endungen
    // eingetragen sind.
    //
    // Ob auch die Haken von dort kommen, steht in takeChecks. Beim Öffnen und
    // nach dem Übernehmen ja; bei einem bloßen Fensterwechsel nein -- sonst
    // wäre eine noch nicht übernommene Auswahl fort, nur weil jemand kurz in
    // ein anderes Fenster gesehen hat.
    size_t Refresh(Dialog& dialog, bool takeChecks)
    {
        const std::vector<FileType>& types = FileTypes();

        size_t registered = 0;
        size_t defaults = 0;
        for (size_t i = 0; i < types.size(); ++i)
        {
            const bool on = IsRegistered(dialog.scope, types[i].extension);
            const bool isDefault = IsDefaultHandler(types[i].extension);
            registered += on ? 1 : 0;
            defaults += isDefault ? 1 : 0;

            if (takeChecks)
                SendMessageW(dialog.boxes[i], BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);

            // Das Kästchen bleibt trotz fehlender Erweiterung bedienbar: wer
            // sie gleich nachinstalliert, soll die Zuordnung schon jetzt
            // treffen können, und wer bereits eingetragen ist, muss sie auch
            // wieder loswerden können.
            const bool missing = types[i].store && !CanDecode(types[i].extension);
            SetWindowTextW(dialog.boxes[i], LabelFor(types[i], isDefault, missing).c_str());
        }

        SetWindowTextW(dialog.status, StatusText(dialog.scope, registered, defaults > 0).c_str());

        // Ohne Eintrag führt der Weg in die Einstellungen ins Leere: dort steht
        // die Bildanzeige dann gar nicht.
        EnableWindow(dialog.defaults, registered > 0);
        return registered;
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
        for (HWND box : dialog.boxes)
        {
            MoveWindow(box, margin, y, width, rowHeight, TRUE);
            y += rowHeight;
        }
        y += gap;

        MoveWindow(dialog.status, margin, y, width, dialog.statusHeight, TRUE);

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
            { dialog.close, Scaled(dialog, kCloseWidth) },
            { dialog.defaults, Scaled(dialog, kDefaultsWidth) },
            { dialog.apply, Scaled(dialog, kApplyWidth) },
        };
        for (const auto& button : buttons)
        {
            MoveWindow(button.window, right - button.width, buttonY, button.width, buttonHeight,
                       TRUE);
            right -= button.width + buttonGap;
        }
    }

    void OnApply(Dialog& dialog)
    {
        const std::vector<FileType>& types = FileTypes();

        std::vector<std::wstring> wanted;
        for (size_t i = 0; i < types.size(); ++i)
        {
            if (SendMessageW(dialog.boxes[i], BM_GETCHECK, 0, 0) == BST_CHECKED)
                wanted.emplace_back(types[i].extension);
        }

        const HCURSOR busy = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
        std::wstring error;
        const bool ok = ApplyRegistration(dialog.scope, wanted, error);
        SetCursor(busy);

        if (!ok)
            MessageBoxW(dialog.hwnd, error.c_str(), kAppTitle, MB_ICONWARNING | MB_OK);

        // Die Haken kommen anschließend wieder aus der Registrierung, nicht aus
        // dem Wunsch: nach einem halb geglückten Durchgang zeigt das Fenster,
        // was wirklich dasteht.
        Refresh(dialog, true);
    }

    void OnDefaults(Dialog& dialog)
    {
        std::wstring error;
        if (!ShowDefaultAppsUI(dialog.scope, error))
            MessageBoxW(dialog.hwnd, error.c_str(), kAppTitle, MB_ICONWARNING | MB_OK);
    }

    void OnCommand(Dialog& dialog, int id)
    {
        switch (id)
        {
        case kIdApply:
            OnApply(dialog);
            break;

        case kIdDefaults:
            OnDefaults(dialog);
            break;

        case kIdClose:
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

        case WM_ACTIVATE:
            // Wer aus den Windows-Einstellungen zurückkommt, hat dort
            // womöglich den Standard geändert. Beim Zurückkehren steht die
            // Anzeige deshalb neu, statt bis zum nächsten Öffnen zu lügen.
            if (LOWORD(wParam) != WA_INACTIVE && dialog->ready)
                Refresh(*dialog, false);
            break;

        case WM_CTLCOLORBTN:
        case WM_CTLCOLORSTATIC:
        {
            // Der Text wird mit der Hintergrundfarbe des Gerätekontexts
            // unterlegt. Ohne diese beiden Zeilen stünden die Beschriftungen
            // in einem weißen Kasten auf grauem Grund -- die Farbe des
            // Fensters allein genügt nicht.
            const HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
            SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
        }

        case WM_CLOSE:
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

        // Ohne Dialogvorlage gibt es keinen Standardknopf, den die
        // Eingabetaste von selbst fände -- sie wird deshalb von Hand auf den
        // Knopf gelegt, der gerade den Fokus hat. Auf einem Kästchen schaltet
        // sie um, wie es die Leertaste ohnehin tut.
        const HWND focus = GetFocus();
        for (HWND box : dialog.boxes)
        {
            if (focus == box)
            {
                SendMessageW(box, BM_CLICK, 0, 0);
                return true;
            }
        }

        int id = kIdApply;
        if (focus == dialog.close)
            id = kIdClose;
        else if (focus == dialog.defaults)
            id = kIdDefaults;
        OnCommand(dialog, id);
        return true;
    }
}

void ShowAssociationDialog(HWND owner)
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
        // Anders als das Hauptfenster und die Seitenansicht zeichnet dieses
        // Fenster seine Fläche nicht selbst: darauf stehen die gewöhnlichen
        // Bedienelemente von Windows, und sie erwarten deren Grundfarbe.
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
    dialog.scope = ProcessScope();
    dialog.dpi = GetDpiForWindow(owner);
    if (dialog.dpi == 0)
        dialog.dpi = 96;

    dialog.font = CreateFontW(-MulDiv(9, static_cast<int>(dialog.dpi), 72), 0, 0, 0, FW_NORMAL,
                              FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
    if (dialog.font == nullptr)
        return;

    // Erst messen, dann das Fenster anlegen: seine Höhe ergibt sich aus dem
    // Kopftext, der Zahl der Endungen und der Schrift, nicht umgekehrt.
    const int margin = Scaled(dialog, kMargin);
    const int gap = Scaled(dialog, kGap);
    const int clientWidth = Scaled(dialog, kWidth);
    const int textWidth = clientWidth - 2 * margin;

    const std::vector<FileType>& types = FileTypes();

    const std::wstring header = HeaderText(dialog.scope);
    dialog.headerHeight = TextHeight(dialog.font, header, textWidth);
    dialog.lineHeight = TextHeight(dialog.font, L"Ag", textWidth);
    dialog.statusHeight =
        std::max(TextHeight(dialog.font, StatusText(dialog.scope, types.size(), true), textWidth),
                 TextHeight(dialog.font, StatusText(dialog.scope, types.size(), false), textWidth));

    const int rowHeight = std::max(Scaled(dialog, kRowHeight), dialog.lineHeight);
    const int clientHeight = margin + dialog.headerHeight + gap +
                             static_cast<int>(types.size()) * rowHeight + gap +
                             dialog.statusHeight + gap + Scaled(dialog, kButtonHeight) + margin;

    // Fester Rahmen ohne Größenzug: an diesem Fenster gibt es nichts zu ziehen,
    // und ein Rahmen, der sich anfassen lässt, verspricht das Gegenteil.
    constexpr DWORD style = WS_CAPTION | WS_SYSMENU;
    dialog.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kDialogClass, L"Dateizuordnungen", style,
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
    for (size_t i = 0; i < types.size(); ++i)
    {
        const HWND box = CreateWindowExW(
            0, L"BUTTON", types[i].extension,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, dialog.hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdFirstBox + static_cast<int>(i))),
            instance, nullptr);
        dialog.boxes.push_back(box);
    }
    dialog.status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
                                    dialog.hwnd, nullptr, instance, nullptr);

    struct ButtonSpec
    {
        const wchar_t* text;
        int id;
        HWND* target;
        DWORD extra;
    };
    const ButtonSpec specs[] = {
        { L"Ü&bernehmen", kIdApply, &dialog.apply, BS_DEFPUSHBUTTON },
        { L"Als &Standard festlegen …", kIdDefaults, &dialog.defaults, 0 },
        { L"S&chließen", kIdClose, &dialog.close, 0 },
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

    // Steht noch nichts in der Registrierung, sind die gängigen Formate
    // vorgehakt: der übliche Wunsch ist dann ein einziger Klick weit weg. Was
    // dasteht, wird dagegen gezeigt, wie es dasteht.
    //
    // Nicht vorgehakt wird, wofür die Erweiterung aus dem Store fehlt. Von
    // selbst soll die Anwendung kein Format an sich ziehen, das sie hier nur
    // mit einer Fehlermeldung beantworten könnte.
    if (Refresh(dialog, true) == 0)
    {
        for (size_t i = 0; i < types.size(); ++i)
        {
            const bool wanted =
                types[i].standard && (!types[i].store || CanDecode(types[i].extension));
            SendMessageW(dialog.boxes[i], BM_SETCHECK, wanted ? BST_CHECKED : BST_UNCHECKED, 0);
        }
    }

    // Von Hand modal, wie die Seitenansicht: das Hauptfenster wird gesperrt,
    // und die Schleife läuft, bis das Fenster geschlossen ist.
    EnableWindow(owner, FALSE);
    ShowWindow(dialog.hwnd, SW_SHOW);
    SetFocus(dialog.apply);

    // Erst jetzt darf WM_ACTIVATE den Stand neu einlesen. Das Anzeigen selbst
    // löst die Meldung aus, und käme sie durch, hätte sie die eben gesetzten
    // Vorhaken gleich wieder aus der Registrierung überschrieben -- also
    // fortgenommen, denn dort steht ja noch nichts.
    dialog.ready = true;

    MSG msg{};
    while (dialog.running && GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        if (msg.message == WM_KEYDOWN && OnKey(dialog, msg.wParam))
            continue;

        // IsDialogMessage liefert das Blättern mit der Tabulatortaste, die
        // Leertaste auf den Kästchen und die Kurztasten der Beschriftungen.
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
