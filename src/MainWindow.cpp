#include "MainWindow.h"

#include "AssociationDialog.h"
#include "PrintPreview.h"
#include "Resource.h"
#include "SettingsDialog.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr wchar_t kWindowClass[] = L"BildanzeigeMainWindow";
    constexpr wchar_t kAppTitle[] = L"Bildanzeige";

    // Ein Rad-Rasten bzw. ein Tastendruck auf Plus/Minus.
    constexpr float kZoomStep = 1.25f;

    constexpr UINT_PTR kAnimationTimer = 1;
    constexpr UINT_PTR kLoadingTimer = 2;

    // Eigener Befehl im Fenstermenü. Windows benutzt die unteren vier Bits von
    // wParam in WM_SYSCOMMAND für sich, und alles ab 0xF000 gehört ihm ohnehin
    // -- deshalb eine Zahl darunter und ein Vielfaches von 16.
    constexpr UINT kSysCmdAssociations = 0x1010;

    // Meldung des Hintergrund-Threads, dass ein Auftrag fertig ist.
    constexpr UINT kDecodeReady = WM_APP + 1;

    // So lange bleibt das vorige Bild stehen, bevor der Ladezustand sichtbar
    // wird. Darunter liegt praktisch jedes Foto; erst was länger braucht,
    // soll überhaupt als Warten in Erscheinung treten.
    constexpr UINT kLoadingDelayMs = 150;

    // Fenstergröße bei 96 dpi, solange noch kein Bild da ist; auf anderen
    // Stufen entsprechend vervielfacht. Sobald ein Bild dasteht, gibt dessen
    // Größe das Maß vor.
    constexpr int kInitialClientWidth = 1000;
    constexpr int kInitialClientHeight = 660;

    // Ein Bild von wenigen Pixeln soll das Fenster nicht auf einen Streifen
    // zusammenziehen: so hoch bleibt die Bildfläche mindestens (bei 96 dpi).
    // Für die Breite sorgt die Icon-Leiste selbst.
    constexpr int kMinImageHeight = 120;

    LONGLONG QpcNow()
    {
        LARGE_INTEGER counter{};
        QueryPerformanceCounter(&counter);
        return counter.QuadPart;
    }

    // Von der Kommandozeile kommt der Pfad oft relativ ("testdata\bild.png").
    // Der Ordner der Nachbardateien ließe sich daraus nicht bestimmen, sobald
    // sich das Arbeitsverzeichnis unterscheidet.
    std::wstring MakeAbsolute(const std::wstring& path)
    {
        const DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
        if (needed == 0)
            return path;

        std::wstring full(needed, L'\0');
        const DWORD length = GetFullPathNameW(path.c_str(), needed, full.data(), nullptr);
        if (length == 0 || length >= needed)
            return path;

        full.resize(length);
        return full;
    }
}

bool MainWindow::Create(HINSTANCE instance, int showCmd)
{
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&wic_));
    if (FAILED(hr))
    {
        MessageBoxW(nullptr, (L"WIC ist nicht verfügbar.\n\n" + FormatHResult(hr)).c_str(),
                    kAppTitle, MB_ICONERROR | MB_OK);
        return false;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = &MainWindow::WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;   // Direct2D zeichnet die gesamte Fläche.
    // Symbol der Fensterklasse: was zu sehen ist, bis das Fenster in WM_CREATE
    // seine eigenen, zur DPI-Stufe passenden Symbole setzt. Das kleine ist
    // eigens angegeben, weil Windows es sonst aus dem großen herunterrechnet
    // -- 32 auf 16 verschmiert genau die feinen Striche, für die die .ico ein
    // eigenes 16er-Bild mitbringt.
    wc.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                             GetSystemMetrics(SM_CXICON),
                                             GetSystemMetrics(SM_CYICON), LR_SHARED));
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON),
                                               GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    wc.lpszClassName = kWindowClass;
    if (RegisterClassExW(&wc) == 0)
        return false;

    hwnd_ = CreateWindowExW(
        WS_EX_ACCEPTFILES, kWindowClass, kAppTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr)
        return false;

    // Die Größe wird erst jetzt gesetzt: vorher steht nicht fest, auf welchem
    // Bildschirm das Fenster landet. Bei 150 % wäre ein fest in Pixeln
    // angegebenes Fenster nur zwei Drittel so groß wie gemeint. Der Rahmen
    // wächst dabei nicht im selben Verhältnis wie die Bildfläche, deshalb
    // rechnet ihn AdjustWindowRectExForDpi zur gewünschten Fläche hinzu.
    const UINT dpi = GetDpiForWindow(hwnd_);
    const float dpiScale = static_cast<float>(dpi > 0 ? dpi : 96) / 96.0f;
    RECT desired = { 0, 0, static_cast<LONG>(kInitialClientWidth * dpiScale),
                     static_cast<LONG>(kInitialClientHeight * dpiScale) };
    if (AdjustWindowRectExForDpi(&desired, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_ACCEPTFILES, dpi))
    {
        // Startplatz ist immer die obere linke Ecke -- rcWork, nicht rcMonitor,
        // damit eine oben oder links angedockte Taskleiste die Titelzeile nicht
        // verdeckt. Bei nur einem Bildschirm ist beides dieselbe Ecke.
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        LONG left = 0;
        LONG top = 0;
        UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
        if (GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), &info))
        {
            left = info.rcWork.left;
            top = info.rcWork.top;
        }
        else
        {
            flags |= SWP_NOMOVE;
        }

        SetWindowPos(hwnd_, nullptr, left, top, desired.right - desired.left,
                     desired.bottom - desired.top, flags);

        // Der unsichtbare Anfasserrand wird herausgerechnet, damit der
        // sichtbare Rahmen bündig in der Ecke sitzt und nicht ein paar Pixel
        // weiter rechts. Siehe FrameOverhang.
        if ((flags & SWP_NOMOVE) == 0)
        {
            const RECT overhang = FrameOverhang();
            SetWindowPos(hwnd_, nullptr, left - overhang.left, top - overhang.top, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    ShowWindow(hwnd_, showCmd);
    UpdateWindow(hwnd_);
    return true;
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE)
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* self = static_cast<MainWindow*>(create->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr)
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    return self->HandleMessage(msg, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        qpcFrequency_ = frequency.QuadPart > 0 ? frequency.QuadPart : 1;

        const HRESULT hr = view_.Initialize(hwnd_, wic_.Get());
        if (FAILED(hr))
        {
            MessageBoxW(hwnd_, (L"Direct2D konnte nicht initialisiert werden.\n\n" +
                                FormatHResult(hr)).c_str(),
                        kAppTitle, MB_ICONERROR | MB_OK);
            return -1;
        }
        if (FAILED(toolbar_.CreateResources(view_.Factory())))
            return -1;

        decoder_.Start(hwnd_, kDecodeReady);

        // Die Dateizuordnungen hängen im Fenstermenü und nicht in der
        // Icon-Leiste: die zeigt, was mit dem Bild geschieht, das gerade
        // dasteht. Sich als Betrachter einzutragen ist dagegen eine Sache der
        // Einrichtung -- einmal getroffen, dann nie wieder angerührt. Der
        // Eintrag steht vor "Schließen"; misslingt das (ein Fenstermenü ohne
        // SC_CLOSE gibt es eigentlich nicht), wird er hinten angehängt.
        if (HMENU menu = GetSystemMenu(hwnd_, FALSE))
        {
            constexpr wchar_t caption[] = L"&Dateizuordnungen …";
            if (!InsertMenuW(menu, SC_CLOSE, MF_BYCOMMAND | MF_STRING, kSysCmdAssociations,
                             caption))
            {
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, kSysCmdAssociations, caption);
            }
            else
            {
                InsertMenuW(menu, SC_CLOSE, MF_BYCOMMAND | MF_SEPARATOR, 0, nullptr);
            }
        }

        ApplyDpi(GetDpiForWindow(hwnd_));
        CreateTooltip();
        UpdateToolbarState();
        return 0;
    }

    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
        {
            // Im Symbolzustand ist nichts zu sehen; der Takt kostete nur Strom.
            minimized_ = true;
            KillTimer(hwnd_, kAnimationTimer);
            return 0;
        }
        if (minimized_)
        {
            minimized_ = false;
            if (playing_)
            {
                // Nach dem Wiederherstellen frisch ansetzen, statt die ganze
                // Zeit im Symbolzustand als Rückstand nachzuholen.
                nextDueTicks_ = QpcNow();
                ScheduleNextFrame();
            }
        }
        view_.Resize(LOWORD(lParam), HIWORD(lParam));
        LayoutToolbar();
        // Der Einpass-Maßstab hängt am Fenster: nach dem Größenändern kann
        // sich entscheiden, ob sich überhaupt noch herauszoomen lässt.
        ApplyButtonStates();
        UpdateTitle();
        return 0;

    case WM_TIMER:
        if (wParam == kAnimationTimer)
        {
            OnAnimationTick();
            return 0;
        }
        if (wParam == kLoadingTimer)
        {
            OnLoadingDelay();
            return 0;
        }
        break;

    case kDecodeReady:
        OnDecodeReady();
        return 0;

    case WM_MOUSEMOVE:
        OnMouseMove(static_cast<float>(GET_X_LPARAM(lParam)),
                    static_cast<float>(GET_Y_LPARAM(lParam)));
        return 0;

    case WM_MOUSELEAVE:
        OnMouseLeave();
        return 0;

    case WM_LBUTTONDOWN:
        OnLeftButtonDown(static_cast<float>(GET_X_LPARAM(lParam)),
                         static_cast<float>(GET_Y_LPARAM(lParam)));
        return 0;

    case WM_LBUTTONUP:
        OnLeftButtonUp(static_cast<float>(GET_X_LPARAM(lParam)),
                       static_cast<float>(GET_Y_LPARAM(lParam)));
        return 0;

    case WM_LBUTTONDBLCLK:
        OnLeftButtonDoubleClick(static_cast<float>(GET_X_LPARAM(lParam)),
                                static_cast<float>(GET_Y_LPARAM(lParam)));
        return 0;

    case WM_MOUSEWHEEL:
        // Das Rad liefert Bildschirmkoordinaten, nicht Fensterkoordinaten.
        OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam),
                     POINT{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
        return 0;

    case WM_CAPTURECHANGED:
        OnCaptureChanged();
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && OnSetCursor())
            return TRUE;
        break;

    case WM_PAINT:
        OnPaint();
        return 0;

    case WM_ERASEBKGND:
        return 1;   // Verhindert Flackern; Direct2D füllt ohnehin alles.

    case WM_DPICHANGED:
        OnDpiChanged(LOWORD(wParam), reinterpret_cast<const RECT*>(lParam));
        return 0;

    case WM_DISPLAYCHANGE:
        // Auflösung oder Anordnung der Bildschirme hat gewechselt. Das
        // Render-Target hängt an der alten Ausgabe; es neu aufzubauen kostet
        // hier nichts und erspart ein schwarzes Fenster.
        RecreateDeviceResources();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;

    case WM_SYSCOMMAND:
        // Maskiert verglichen: die unteren vier Bits sind nicht die eigenen.
        if ((wParam & 0xFFF0) == kSysCmdAssociations)
        {
            // Angehalten wird wie vor dem Druckdialog: das Fenster ist zwar
            // gesperrt, aber der Zeitgeber schlägt in der Nachrichtenschleife
            // des Dialogs weiter, und eine Animation, die hinter einem
            // gesperrten Fenster weiterläuft, kostet nur Strom.
            StopPlayback();
            ShowAssociationDialog(hwnd_);
            ApplyButtonStates();
            UpdateTitle();
            return 0;
        }
        break;

    case WM_KEYDOWN:
        OnKeyDown(wParam);
        return 0;

    case WM_DROPFILES:
        OnDropFiles(reinterpret_cast<HDROP>(wParam));
        return 0;

    case WM_DESTROY:
        StopPlayback();
        // Erst den Thread einholen, dann die Ressourcen abräumen: er hält
        // während eines Auftrags eine eigene WIC-Fabrik in der Hand.
        decoder_.Stop();
        animator_.Reset();
        view_.Shutdown();
        if (iconLarge_ != nullptr)
            DestroyIcon(iconLarge_);
        if (iconSmall_ != nullptr)
            DestroyIcon(iconSmall_);
        iconLarge_ = nullptr;
        iconSmall_ = nullptr;
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

void MainWindow::OnPaint()
{
    PAINTSTRUCT ps{};
    BeginPaint(hwnd_, &ps);

    const HRESULT hr = view_.Render(&toolbar_);
    if (hr == D2DERR_RECREATE_TARGET)
    {
        // Geräteressourcen verloren (GPU-Reset, Treiberwechsel). Neu aufbauen
        // und einen weiteren Durchgang anfordern -- sonst bleibt das Fenster schwarz.
        RecreateDeviceResources();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    EndPaint(hwnd_, &ps);
}

// Der Wiederaufbau nach einem Geräteverlust.
//
// Mit dem Gerät ist auch die Bitmap fort. RenderView hält die WIC-Quelle
// nicht vor -- bei einem großen Bild wäre das dauerhaft eine viertel
// Gigabyte für einen Fall, der jahrelang ausbleiben kann. Das Bild wird
// stattdessen neu geholt, und zwar auf demselben Weg wie sonst auch.
void MainWindow::RecreateDeviceResources()
{
    view_.DiscardDeviceResources();
    toolbar_.DiscardDeviceResources();
    view_.CreateDeviceResources();
    LayoutToolbar();

    if (!document_.IsOpen() || view_.HasImage())
        return;

    if (animator_.IsActive())
    {
        // Die laufende Wiedergabe wird dabei nicht angehalten: die Leinwand
        // liegt bereits komponiert vor, es ist nur der Upload zu wiederholen.
        std::wstring error;
        ShowAnimationFrame(frameIndex_, error);
    }
    else
    {
        RequestFrame(frameIndex_);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::OnKeyDown(WPARAM key)
{
    const bool control = GetKeyState(VK_CONTROL) < 0;

    // Verschoben wird mit Strg und Pfeiltaste. Die bloßen Pfeiltasten gehören
    // dem Bildwechsel im Ordner: dieselbe Taste je nach Zoomstufe verschieden
    // zu belegen wäre für den Benutzer nicht absehbar.
    if (control && view_.View().CanPan())
    {
        constexpr float kStep = 60.0f;
        switch (key)
        {
        case VK_LEFT:   view_.View().PanBy(+kStep, 0.0f); AfterViewChange(); return;
        case VK_RIGHT:  view_.View().PanBy(-kStep, 0.0f); AfterViewChange(); return;
        case VK_UP:     view_.View().PanBy(0.0f, +kStep); AfterViewChange(); return;
        case VK_DOWN:   view_.View().PanBy(0.0f, -kStep); AfterViewChange(); return;
        default:        break;
        }
    }

    switch (key)
    {
    // Esc verlässt zuerst das Vollbild. Wer es geöffnet hat, erwartet dort
    // den Rückweg -- das Fenster gleich ganz zu schließen wäre eine böse
    // Überraschung, weil im Vollbild kein Rahmen daran erinnert, dass noch ein
    // Fenster darunterliegt.
    case VK_ESCAPE:
        if (fullscreen_)
            ToggleFullscreen();
        else
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        break;

    case VK_F11:
        ToggleFullscreen();
        break;

    case VK_SPACE:
        TogglePlayback();
        break;

    // Mit Strg ist die Pfeiltaste zum Verschieben gedacht. Gibt es nichts zu
    // verschieben, läuft der Tastendruck hier durch -- dann aber ins Leere,
    // statt unversehens die Datei zu wechseln.
    case VK_LEFT:
        if (!control)
            StepFile(-1);
        break;

    case VK_RIGHT:
        if (!control)
            StepFile(+1);
        break;

    case VK_ADD:
    case VK_OEM_PLUS:
        Execute(ToolbarCommand::ZoomIn);
        break;

    case VK_SUBTRACT:
    case VK_OEM_MINUS:
        Execute(ToolbarCommand::ZoomOut);
        break;

    case 'B':
        if (control)
            Execute(ToolbarCommand::FitToWindow);
        break;

    case '0':
        if (control)
            Execute(ToolbarCommand::ActualSize);
        break;

    case VK_NEXT:    // Bild ab
        StepFrame(+1);
        break;

    case VK_PRIOR:   // Bild auf
        StepFrame(-1);
        break;

    case VK_HOME:
        ShowFrame(0);
        break;

    case VK_END:
        if (document_.IsOpen())
            ShowFrame(document_.FrameCount() - 1);
        break;

    case 'L':
        if (control)
            Execute(ToolbarCommand::RotateLeft);
        break;

    case 'R':
        if (control)
            Execute(ToolbarCommand::RotateRight);
        break;

    case 'P':
        if (control)
            Execute(ToolbarCommand::Print);
        break;

    default:
        break;
    }
}

void MainWindow::OnMouseMove(float x, float y)
{
    if (!trackingMouse_)
    {
        // Ohne WM_MOUSELEAVE bliebe die Hervorhebung hängen, wenn der Zeiger
        // das Fenster verlässt, ohne vorher die Leiste zu passieren.
        TRACKMOUSEEVENT track{};
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = hwnd_;
        if (TrackMouseEvent(&track))
            trackingMouse_ = true;
    }

    if (panning_)
    {
        // Verschieben zählt in ganzen Bildschirmpixeln, damit sich der Zeiger
        // nicht durch aufsummierte Rundungsreste vom Bild löst.
        const POINT now = { static_cast<LONG>(x), static_cast<LONG>(y) };
        const float dx = static_cast<float>(now.x - panAnchor_.x);
        const float dy = static_cast<float>(now.y - panAnchor_.y);
        panAnchor_ = now;
        if (dx != 0.0f || dy != 0.0f)
        {
            view_.View().PanBy(dx, dy);
            AfterViewChange();
        }
        return;
    }

    if (toolbar_.SetHover(toolbar_.HitTest(x, y)))
        InvalidateToolbar();
}

void MainWindow::OnMouseLeave()
{
    trackingMouse_ = false;

    // Nur die Hervorhebung zurücksetzen. Einen laufenden Tastendruck hier zu
    // verwerfen würde ihn verschlucken, sobald der Zeiger kurz das Fenster
    // verlässt -- der Abbruch wird in OnLeftButtonUp anhand der Zeigerposition
    // entschieden, der unfreiwillige Verlust über WM_CAPTURECHANGED.
    if (toolbar_.SetHover(ToolbarCommand::None))
        InvalidateToolbar();
}

void MainWindow::OnCaptureChanged()
{
    panning_ = false;
    if (toolbar_.SetPressed(ToolbarCommand::None))
        InvalidateToolbar();
}

bool MainWindow::OnSetCursor()
{
    // Während ein Bild geholt wird, sagt der Zeiger es als erster -- er ist
    // die einzige Rückmeldung, die schon vor der Frist des Ladezustands da ist.
    if (pendingToken_ != 0)
    {
        SetCursor(LoadCursorW(nullptr, IDC_APPSTARTING));
        return true;
    }

    POINT pt{};
    if (!GetCursorPos(&pt) || !ScreenToClient(hwnd_, &pt))
        return false;

    const D2D1_RECT_F strip = toolbar_.StripRect();
    const bool overStrip = static_cast<float>(pt.y) >= strip.top;
    if (!panning_ && (overStrip || !view_.View().CanPan()))
        return false;   // Klassenzeiger genügt

    SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
    return true;
}

void MainWindow::OnLeftButtonDown(float x, float y)
{
    const ToolbarCommand hit = toolbar_.HitTest(x, y);
    if (hit != ToolbarCommand::None)
    {
        SetCapture(hwnd_);
        if (toolbar_.SetPressed(hit))
            InvalidateToolbar();
        return;
    }

    // Außerhalb der Leiste: den Ausschnitt ziehen, sofern es einen gibt.
    const D2D1_RECT_F strip = toolbar_.StripRect();
    if (y >= strip.top || !view_.View().CanPan())
        return;

    panning_ = true;
    panAnchor_ = { static_cast<LONG>(x), static_cast<LONG>(y) };
    SetCapture(hwnd_);
    SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
}

void MainWindow::OnLeftButtonUp(float x, float y)
{
    if (panning_)
    {
        EndPan();
        return;
    }

    // Zuerst sichern: ReleaseCapture löst WM_CAPTURECHANGED aus, was den
    // Zustand noch vor der Auswertung zurücksetzt.
    const ToolbarCommand pressed = toolbar_.Pressed();
    if (pressed == ToolbarCommand::None)
        return;

    ReleaseCapture();
    InvalidateToolbar();

    // Nur auslösen, wenn der Zeiger beim Loslassen noch auf demselben Knopf
    // steht -- so lässt sich ein versehentlicher Klick durch Wegziehen abbrechen.
    if (toolbar_.HitTest(x, y) == pressed)
        Execute(pressed);
}

void MainWindow::EndPan()
{
    panning_ = false;
    ReleaseCapture();
}

void MainWindow::OnLeftButtonDoubleClick(float x, float y)
{
    // Bei zwei schnellen Klicks ersetzt Windows den zweiten WM_LBUTTONDOWN
    // durch WM_LBUTTONDBLCLK. Ohne diese Weiterleitung ginge der zweite Klick
    // verloren -- wer zweimal rasch auf "drehen" tippt, käme nur auf 90 statt
    // 180 Grad.
    if (toolbar_.HitTest(x, y) != ToolbarCommand::None)
    {
        OnLeftButtonDown(x, y);
        return;
    }

    // Auf der Bildfläche schaltet er das Vollbild -- über der Leiste nicht,
    // dort wäre er nur das versehentliche Nachfassen auf einem grauen Knopf.
    const D2D1_RECT_F strip = toolbar_.StripRect();
    if (y < strip.top)
        ToggleFullscreen();
}

void MainWindow::OnMouseWheel(int delta, POINT screen)
{
    if (!view_.HasImage() || delta == 0)
        return;

    POINT pt = screen;
    if (!ScreenToClient(hwnd_, &pt))
        return;

    // Über der Leiste gibt es keinen Bildpunkt, an dem sich der Zoom
    // festmachen ließe; der Anker wandert dann an den unteren Bildrand.
    const D2D1_RECT_F strip = toolbar_.StripRect();
    const float anchorY = std::min(static_cast<float>(pt.y), strip.top);

    const float notches = static_cast<float>(delta) / static_cast<float>(WHEEL_DELTA);
    view_.View().ZoomBy(std::pow(kZoomStep, notches),
                        D2D1::Point2F(static_cast<float>(pt.x), anchorY));
    AfterViewChange();
}

void MainWindow::Zoom(float factor)
{
    view_.View().ZoomBy(factor);
    AfterViewChange();
}

void MainWindow::AfterViewChange()
{
    ApplyButtonStates();
    UpdateTitle();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::Execute(ToolbarCommand command)
{
    switch (command)
    {
    case ToolbarCommand::PreviousFile:
        StepFile(-1);
        break;

    case ToolbarCommand::NextFile:
        StepFile(+1);
        break;

    case ToolbarCommand::PreviousPage:
        StepFrame(-1);
        break;

    case ToolbarCommand::PlayPause:
        TogglePlayback();
        break;

    case ToolbarCommand::NextPage:
        StepFrame(+1);
        break;

    case ToolbarCommand::ZoomOut:
        Zoom(1.0f / kZoomStep);
        break;

    case ToolbarCommand::ZoomIn:
        Zoom(kZoomStep);
        break;

    case ToolbarCommand::FitToWindow:
        view_.View().FitToWindow();
        AfterViewChange();
        break;

    case ToolbarCommand::ActualSize:
        view_.View().ActualSize();
        AfterViewChange();
        break;

    // Nach einer Vierteldrehung steht das Bild hochkant statt quer -- das
    // Fenster geht mit, sonst bliebe rechts und links ein breiter Streifen
    // Hintergrund stehen.
    case ToolbarCommand::RotateLeft:
        view_.View().Rotate(-1);
        FitWindowToImage();
        AfterViewChange();
        break;

    case ToolbarCommand::RotateRight:
        view_.View().Rotate(+1);
        FitWindowToImage();
        AfterViewChange();
        break;

    case ToolbarCommand::Print:
        PrintCurrent();
        break;

    case ToolbarCommand::Fullscreen:
        ToggleFullscreen();
        break;

    case ToolbarCommand::Settings:
        // Angehalten wie vor dem Druckdialog und den Dateizuordnungen: das
        // Fenster ist zwar gesperrt, der Zeitgeber schlägt in der
        // Nachrichtenschleife des Dialogs aber weiter, und eine Animation
        // hinter einem gesperrten Fenster kostet nur Strom.
        StopPlayback();
        ShowSettingsDialog(hwnd_);
        ApplyButtonStates();
        break;

    default:
        break;
    }
}

// Gedruckt wird, was zu sehen ist: die gezeigte Seite in der Drehung der
// Anzeige. Zoom und Ausschnitt bleiben außen vor -- sie sind die Lupe, mit der
// man das Bild betrachtet, nicht das Bild.
//
// Der Weg führt über die Seitenansicht und nicht geradewegs in den
// Druckdialog. Windows 11 hat den Dialog gegen einen eigenen ausgetauscht, und
// dessen Vorschaufeld bleibt bei jeder Win32-Anwendung leer ("Diese App
// unterstützt keine Seitenansicht") -- es will die Seiten schon haben, bevor
// gedruckt wird, und eine GDI-Anwendung erzeugt sie erst danach. Die eigene
// Ansicht davor schließt die Lücke.
void MainWindow::PrintCurrent()
{
    if (!document_.IsOpen())
        return;

    // Vor dem Dialog anhalten. Er ist modal, aber der Zeitgeber schlägt in
    // seiner Nachrichtenschleife weiter -- die Seite wäre unter dem Auftrag
    // fortgewandert, und gedruckt käme etwas anderes heraus als das, was beim
    // Drücken zu sehen war.
    StopPlayback();

    PrintJob job;
    job.document = &document_;
    job.currentPage = frameIndex_;
    job.rotationQuarters = view_.View().Rotation();

    ComPtr<IWICBitmapSource> composed;
    if (animator_.IsActive())
    {
        std::wstring composeError;
        composed = animator_.Compose(frameIndex_, composeError);
        if (!composed)
        {
            MessageBoxW(hwnd_, composeError.c_str(), kAppTitle, MB_ICONWARNING | MB_OK);
            return;
        }
        job.composed = composed.Get();
    }

    std::wstring error;
    if (ShowPrintPreview(hwnd_, wic_.Get(), job, error) == PrintOutcome::Failed)
        MessageBoxW(hwnd_, error.c_str(), kAppTitle, MB_ICONWARNING | MB_OK);

    // Der Halt aus StopPlayback steht jetzt auch am Wiedergabeknopf.
    ApplyButtonStates();
    UpdateTitle();
    InvalidateToolbar();
}

// Wie weit das Fensterrechteck über den sichtbaren Rahmen hinausragt.
//
// Seit Windows 10 liegt links, rechts und unten ein unsichtbarer Anfasserrand
// von einigen Pixeln außerhalb dessen, was man sieht -- er gehört zum
// Fensterrechteck, nicht zum Fenster. Wer nur in Fensterrechtecken rechnet,
// setzt das Fenster deshalb neben die Ecke und lässt es ebenso weit über den
// Arbeitsbereich hinausstehen. DWM kennt die sichtbaren Grenzen; hier steht
// der Abstand je Seite, stets nicht-negativ. Bleibt die Auskunft aus, sind es
// lauter Nullen und es wird wie ohne diese Korrektur gerechnet.
RECT MainWindow::FrameOverhang() const
{
    RECT overhang{};
    RECT visible{};
    RECT window{};
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd_, DWMWA_EXTENDED_FRAME_BOUNDS, &visible,
                                        sizeof(visible))) &&
        GetWindowRect(hwnd_, &window))
    {
        overhang.left = std::max<LONG>(visible.left - window.left, 0);
        overhang.top = std::max<LONG>(visible.top - window.top, 0);
        overhang.right = std::max<LONG>(window.right - visible.right, 0);
        overhang.bottom = std::max<LONG>(window.bottom - visible.bottom, 0);
    }
    return overhang;
}

// Das Fenster nimmt die Größe des Bildes an.
//
// Gemeint ist das Bild, wie es dasteht: eine Vierteldrehung vertauscht Breite
// und Höhe, und unter der Bildfläche kommt die Icon-Leiste hinzu. Der Maßstab
// ist dabei 1:1 -- das Render-Target rechnet in Pixeln, ein Bildpunkt ist ein
// Bildschirmpunkt, auch auf einer anderen DPI-Stufe.
//
// Im Vollbild und im maximierten Zustand geschieht nichts: dort ist die Größe
// ausdrücklich gesetzt worden, und sie zu übergehen hieße, den Zustand
// aufzulösen. Passt das Bild nicht auf den Bildschirm, endet das Fenster am
// Arbeitsbereich, und das Bild wird wie gewohnt hineingepasst.
void MainWindow::FitWindowToImage()
{
    if (fullscreen_ || IsZoomed(hwnd_) || IsIconic(hwnd_) || !view_.HasImage())
        return;

    const D2D1_SIZE_F image = view_.View().ShownSize();
    if (image.width <= 0.0f || image.height <= 0.0f)
        return;

    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &info))
        return;

    RECT window{};
    if (!GetWindowRect(hwnd_, &window))
        return;

    // Rahmen und Titelzeile kommen zur Bildfläche hinzu; ihr Maß hängt an der
    // DPI-Stufe des Bildschirms, auf dem das Fenster steht.
    const UINT dpi = GetDpiForWindow(hwnd_);
    const float dpiScale = static_cast<float>(dpi > 0 ? dpi : 96) / 96.0f;
    RECT frame{};
    if (!AdjustWindowRectExForDpi(&frame, static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE)),
                                  FALSE,
                                  static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE)), dpi))
    {
        return;
    }
    const LONG frameWidth = frame.right - frame.left;
    const LONG frameHeight = frame.bottom - frame.top;

    const RECT overhang = FrameOverhang();
    const LONG toolbar = std::lround(toolbar_.Height());

    // Die Obergrenze ist der Arbeitsbereich, nicht der ganze Bildschirm: unter
    // der Taskleiste liegt kein Platz. Verglichen wird der sichtbare Rahmen --
    // der Anfasserrand darf darüber hinausragen, genau wie bei einem
    // maximierten Fenster.
    const LONG maxClientWidth = (info.rcWork.right - info.rcWork.left) - frameWidth +
                                overhang.left + overhang.right;
    const LONG maxClientHeight = (info.rcWork.bottom - info.rcWork.top) - frameHeight +
                                 overhang.top + overhang.bottom;

    const LONG minClientWidth = std::lround(toolbar_.MinimumWidth());
    const LONG minClientHeight = toolbar + std::lround(kMinImageHeight * dpiScale);

    // Bei einem sehr kleinen Bildschirm kann die Untergrenze über der
    // Obergrenze liegen. Dann gilt die Obergrenze: aus dem Fenster
    // hinauszuwachsen wäre schlimmer als eine angeschnittene Leiste.
    const LONG clientWidth = std::min(std::max(std::lround(image.width), minClientWidth),
                                      std::max(maxClientWidth, 1L));
    const LONG clientHeight =
        std::min(std::max(std::lround(image.height) + toolbar, minClientHeight),
                 std::max(maxClientHeight, 1L));

    const LONG width = clientWidth + frameWidth;
    const LONG height = clientHeight + frameHeight;

    // Die obere linke Ecke bleibt liegen, das Fenster wächst nach rechts und
    // unten. Stünde es damit über den Arbeitsbereich hinaus, rückt es so weit
    // zurück, dass es wieder hineinpasst -- die Größe von oben ist so gewählt,
    // dass das immer gelingt.
    LONG left = window.left;
    LONG top = window.top;
    left -= std::max<LONG>((left + width - overhang.right) - info.rcWork.right, 0);
    top -= std::max<LONG>((top + height - overhang.bottom) - info.rcWork.bottom, 0);
    left = std::max(left, info.rcWork.left - overhang.left);
    top = std::max(top, info.rcWork.top - overhang.top);

    SetWindowPos(hwnd_, nullptr, left, top, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

// Das Vollbild nimmt dem Fenster nur den Rahmen und legt es über den ganzen
// Bildschirm. Die Leiste bleibt stehen: sie einzublenden, sobald sich die Maus
// regt, hieße einen zweiten Satz Regeln für Sichtbarkeit, Zeitgeber und
// Treffprüfung zu führen -- und ohne Rahmen und Menü wäre sie der einzige
// sichtbare Rückweg.
void MainWindow::ToggleFullscreen()
{
    if (!fullscreen_)
    {
        placement_.length = sizeof(placement_);
        if (!GetWindowPlacement(hwnd_, &placement_))
            return;

        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &info))
            return;

        savedStyle_ = GetWindowLongPtrW(hwnd_, GWL_STYLE);
        SetWindowLongPtrW(hwnd_, GWL_STYLE, savedStyle_ & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW));

        // rcMonitor, nicht rcWork: die Taskleiste wird mit überdeckt.
        SetWindowPos(hwnd_, HWND_TOP, info.rcMonitor.left, info.rcMonitor.top,
                     info.rcMonitor.right - info.rcMonitor.left,
                     info.rcMonitor.bottom - info.rcMonitor.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        fullscreen_ = true;
    }
    else
    {
        SetWindowLongPtrW(hwnd_, GWL_STYLE, savedStyle_);

        // SetWindowPlacement stellt auch wieder her, was vor dem Vollbild
        // maximiert war -- eine gemerkte Rechteckgröße könnte das nicht.
        SetWindowPlacement(hwnd_, &placement_);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER |
                         SWP_FRAMECHANGED);
        fullscreen_ = false;
    }

    toolbar_.SetFullscreen(fullscreen_);
    InvalidateToolbar();
}

void MainWindow::ApplyDpi(UINT dpi)
{
    toolbar_.SetDpiScale(static_cast<float>(dpi > 0 ? dpi : 96) / 96.0f);
    view_.SetToolbarHeight(toolbar_.Height());
    LayoutToolbar();
    ApplyIcons(dpi);
}

void MainWindow::ApplyIcons(UINT dpi)
{
    // Titelleiste und Taskleiste zeigen das Symbol in der Größe, die zur
    // Stufe des jeweiligen Bildschirms gehört: bei 150 % also 24 statt 16
    // Punkte. Bekäme Windows nur das 16er, zöge es das auf 24 hoch. Die .ico
    // hat 24 fertig dabei -- es genügt, die Größe zu erfragen und das
    // passende Bild daraus zu laden.
    const auto instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE));
    const int bigSize = GetSystemMetricsForDpi(SM_CXICON, dpi);
    const int smallSize = GetSystemMetricsForDpi(SM_CXSMICON, dpi);

    auto load = [&](int size) {
        return static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                             size, size, LR_DEFAULTCOLOR));
    };
    HICON large = load(bigSize);
    HICON mini = load(smallSize);
    if (large == nullptr || mini == nullptr)
    {
        // Dann bleibt das Symbol der Fensterklasse stehen. Kein Grund, deshalb
        // irgendetwas abzubrechen -- es sieht nur eine Spur unschärfer aus.
        if (large != nullptr)
            DestroyIcon(large);
        if (mini != nullptr)
            DestroyIcon(mini);
        return;
    }

    SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large));
    SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(mini));

    // Die alten erst jetzt freigeben: bis zum Setzen der neuen zeigt das
    // Fenster noch auf sie.
    if (iconLarge_ != nullptr)
        DestroyIcon(iconLarge_);
    if (iconSmall_ != nullptr)
        DestroyIcon(iconSmall_);
    iconLarge_ = large;
    iconSmall_ = mini;
}

void MainWindow::LayoutToolbar()
{
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    toolbar_.Layout(static_cast<float>(rc.right - rc.left),
                    static_cast<float>(rc.bottom - rc.top));
    UpdateTooltipRects();
}

void MainWindow::UpdateToolbarState()
{
    const bool multiPage = document_.IsOpen() && document_.FrameCount() > 1;

    // Die Schrittknöpfe verschwinden ganz, wenn es nichts zu blättern gibt --
    // bei einem gewöhnlichen Foto bleibt die Leiste dadurch aufgeräumt.
    toolbar_.SetVisible(ToolbarCommand::PreviousPage, multiPage);
    toolbar_.SetVisible(ToolbarCommand::NextPage, multiPage);
    toolbar_.SetVisible(ToolbarCommand::PlayPause, animator_.IsActive());

    ApplyButtonStates();
    LayoutToolbar();
    InvalidateToolbar();
}

// Nur die Verfügbarkeit, ohne Neuberechnung des Layouts: das läuft bei jeder
// Rad-Rastung durch und soll nicht jedes Mal die Kurzhilfen neu anmelden.
void MainWindow::ApplyButtonStates()
{
    const bool multiPage = document_.IsOpen() && document_.FrameCount() > 1;
    const bool hasImage = view_.HasImage();
    const ViewState& state = view_.View();

    toolbar_.SetEnabled(ToolbarCommand::PreviousFile, folder_.HasNeighbour(-1));
    toolbar_.SetEnabled(ToolbarCommand::NextFile, folder_.HasNeighbour(+1));

    // Bei einer Animation sind die Schrittknöpfe der Einzelschritt und deshalb
    // nur im Halt verfügbar. Sie werden dabei bewusst nicht aus- und wieder
    // eingeblendet: die Leiste ist mittig gesetzt, das Ein- und Ausblenden
    // rückte den Wiedergabeknopf unter dem Zeiger fort, kaum dass man ihn
    // getroffen hat.
    const bool animated = animator_.IsActive();
    toolbar_.SetEnabled(ToolbarCommand::PreviousPage,
                        animated ? !playing_ : (multiPage && frameIndex_ > 0));
    toolbar_.SetEnabled(ToolbarCommand::NextPage,
                        animated ? !playing_
                                 : (multiPage && frameIndex_ + 1 < document_.FrameCount()));
    toolbar_.SetEnabled(ToolbarCommand::PlayPause, animated);
    toolbar_.SetPlaying(playing_);

    // Graue Knöpfe sagen hier zugleich, wie das Bild gerade steht: "Einpassen"
    // ist grau, solange eingepasst ist, "Originalgröße" bei 100 %.
    toolbar_.SetEnabled(ToolbarCommand::ZoomOut, hasImage && state.CanZoomOut());
    toolbar_.SetEnabled(ToolbarCommand::ZoomIn, hasImage && state.CanZoomIn());
    toolbar_.SetEnabled(ToolbarCommand::FitToWindow, hasImage && !state.IsFit());
    toolbar_.SetEnabled(ToolbarCommand::ActualSize, hasImage && !state.IsActualSize());

    toolbar_.SetEnabled(ToolbarCommand::RotateLeft, hasImage);
    toolbar_.SetEnabled(ToolbarCommand::RotateRight, hasImage);

    // Gedruckt wird aus der Datei, gebraucht wird also der offene Decoder --
    // das gezeigte Bild dazu, damit der Knopf nicht bereitsteht, solange nach
    // einem Fehlschlag gar nichts zu sehen ist.
    toolbar_.SetEnabled(ToolbarCommand::Print, hasImage && document_.IsOpen());

    // Das Vollbild hängt am Fenster, nicht am Bild: es bleibt auch dann
    // erreichbar, wenn gerade nichts geladen ist. Für die Einstellungen gilt
    // dasselbe -- sie stehen ohnehin neben der Anzeige.
    toolbar_.SetEnabled(ToolbarCommand::Fullscreen, true);
    toolbar_.SetEnabled(ToolbarCommand::Settings, true);
}

void MainWindow::InvalidateToolbar()
{
    const D2D1_RECT_F strip = toolbar_.StripRect();
    const RECT rc = { static_cast<LONG>(strip.left), static_cast<LONG>(strip.top),
                      static_cast<LONG>(strip.right) + 1, static_cast<LONG>(strip.bottom) + 1 };
    InvalidateRect(hwnd_, &rc, FALSE);
}

void MainWindow::CreateTooltip()
{
    tooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr,
                               WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                               hwnd_, nullptr, nullptr, nullptr);
    if (tooltip_ == nullptr)
        return;

    UINT_PTR id = 1;
    for (const ToolbarButton& button : toolbar_.Buttons())
    {
        TTTOOLINFOW info{};
        info.cbSize = sizeof(info);
        info.uFlags = TTF_SUBCLASS;
        info.hwnd = hwnd_;
        info.uId = id++;
        info.lpszText = const_cast<LPWSTR>(button.tooltip);
        SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
    }
}

void MainWindow::UpdateTooltipRects()
{
    if (tooltip_ == nullptr)
        return;

    UINT_PTR id = 1;
    for (const ToolbarButton& button : toolbar_.Buttons())
    {
        TTTOOLINFOW info{};
        info.cbSize = sizeof(info);
        info.hwnd = hwnd_;
        info.uId = id++;
        // Unsichtbare Knöpfe bekommen ein leeres Rechteck, sonst zeigte die
        // Hilfe weiter auf eine Stelle, an der nichts mehr steht.
        if (button.visible)
        {
            info.rect = { static_cast<LONG>(button.rect.left), static_cast<LONG>(button.rect.top),
                          static_cast<LONG>(button.rect.right),
                          static_cast<LONG>(button.rect.bottom) };
        }
        SendMessageW(tooltip_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&info));
    }
}

// Der Bildschirm hat gewechselt oder seine Skalierung. Die neue Stufe steht in
// der Nachricht -- sie am Fenster zu erfragen wäre ein Umweg über einen
// Zustand, den das System gerade erst mitgeteilt hat.
void MainWindow::OnDpiChanged(UINT dpi, const RECT* suggested)
{
    if (suggested != nullptr)
    {
        // Das Fenster folgt dem Vorschlag des Systems; die Bildfläche rechnet
        // in Pixeln, es ist also nichts weiter umzurechnen.
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    ApplyDpi(dpi);

    // Der Vorschlag des Systems rechnet das ganze Fenster auf die neue Stufe
    // um -- das Bild bleibt dabei aber gleich groß, es zählt in Pixeln. Rahmen
    // und Leiste sind nun anders hoch, also wird das Maß neu genommen.
    FitWindowToImage();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool MainWindow::OpenFile(const std::wstring& path)
{
    std::wstring full = MakeAbsolute(path);

    // Ein Ordner statt einer Datei -- so kommt es an, wenn einer auf das
    // Fenster gezogen oder auf der Kommandozeile übergeben wird. Gemeint ist
    // dann das erste Bild darin.
    if (PathIsDirectoryW(full.c_str()))
    {
        const std::wstring first = folder_.FirstIn(wic_.Get(), full);
        if (first.empty())
        {
            MessageBoxW(hwnd_, L"Der Ordner enthält kein anzeigbares Bild.",
                        kAppTitle, MB_ICONWARNING | MB_OK);
            return false;
        }
        full = first;
    }

    // Auch bei einer Datei, die sich nicht öffnen lässt: erst dadurch bleiben
    // die Nachbarn erreichbar, und man kommt über eine kaputte Datei hinweg,
    // statt in ihr steckenzubleiben.
    folder_.Track(wic_.Get(), full);

    // Vor dem Öffnen anhalten: der laufende Takt griffe sonst während des
    // Wechsels auf einen Decoder zu, den ImageDocument::Open gerade ersetzt.
    StopPlayback();
    animator_.Reset();
    loopsDone_ = 0;

    // Ein noch unterwegs befindliches Bild gehört zur vorigen Datei. Ohne das
    // Verwerfen könnte es nach dem Wechsel eintreffen und das neue überschreiben.
    CancelDecode();

    std::wstring error;
    if (!document_.Open(wic_.Get(), full, error))
    {
        pendingRotation_ = -1;
        view_.ClearImage();
        UpdateTitle();
        UpdateToolbarState();
        InvalidateRect(hwnd_, nullptr, FALSE);
        MessageBoxW(hwnd_, error.c_str(), kAppTitle, MB_ICONWARNING | MB_OK);
        return false;
    }

    frameIndex_ = 0;
    animator_.Load(wic_.Get(), document_);

    // Ein hochkant gehaltenes Foto steht quer in der Datei und trägt die
    // Drehung nur als Vermerk. Sie ist die Ausgangslage, auf die sich ein
    // späteres Drehen von Hand bezieht -- greifen darf sie aber erst, wenn das
    // neue Bild da ist, sonst kippte für einen Augenblick noch das vorige.
    pendingRotation_ = document_.OrientationQuarters();

    if (animator_.IsActive())
    {
        // Die Leinwand wird geleert, damit das erste Einzelbild als
        // Größenwechsel durchgeht und eingepasst anfängt -- auch dann, wenn
        // die vorige Datei zufällig gleich groß war. Bei einem gewöhnlichen
        // Bild geschieht das von selbst, weil dort jedes Bild frisch eingepasst
        // wird; es hier ebenso zu leeren hieße, bei jedem Tastendruck den
        // leeren Hintergrund aufblitzen zu lassen.
        view_.ClearImage();
    }

    if (!ShowFrame(0))
    {
        document_.Close();
        pendingRotation_ = -1;
        view_.ClearImage();
        UpdateTitle();
        UpdateToolbarState();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return false;
    }

    if (animator_.IsActive())
    {
        StartPlayback();
        ApplyButtonStates();
        // ShowFrame hat den Titel gesetzt, als noch nichts lief -- er trüge
        // sonst die Bildzählung des Halts durch die ganze Wiedergabe.
        UpdateTitle();
        InvalidateToolbar();
    }
    return true;
}

// Liefert false nur bei einem Fehlschlag, der sofort feststeht. Ein Bild, das
// im Hintergrund geholt wird, gilt als unterwegs -- ob es ankommt, entscheidet
// sich erst in OnDecodeReady.
bool MainWindow::ShowFrame(UINT index)
{
    if (!document_.IsOpen() || index >= document_.FrameCount())
        return false;

    if (animator_.IsActive())
    {
        // Die Einzelbilder eines GIFs entstehen durch Übereinanderlegen und
        // müssen im Takt der Wiedergabe bereitstehen. Sie sind klein genug,
        // dass sich der Umweg über den Hintergrund-Thread nicht lohnt -- er
        // brächte hier nur die Frage mit, was gilt, wenn der Takt schneller
        // schlägt als die Antwort eintrifft.
        CancelDecode();

        // Wer eine Stelle gezielt ansteuert, will sie ansehen -- die Wiedergabe
        // liefe ihm sonst binnen eines Augenblicks davon.
        StopPlayback();

        std::wstring error;
        if (!ShowAnimationFrame(index, error))
        {
            pendingRotation_ = -1;
            MessageBoxW(hwnd_, error.c_str(), kAppTitle, MB_ICONWARNING | MB_OK);
            return false;
        }

        ApplyPendingRotation();
        UpdateTitle();
        UpdateToolbarState();
        FitWindowToImage();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    RequestFrame(index);
    return true;
}

// Auftrag an den Hintergrund-Thread.
//
// frameIndex_ wandert schon hier mit, nicht erst beim Eintreffen: er ist die
// angesteuerte Seite, nicht die gezeigte. Sonst rechnete ein zweiter
// Tastendruck während des Ladens noch von der alten Seite aus und bliebe
// wirkungslos.
void MainWindow::RequestFrame(UINT index)
{
    frameIndex_ = index;
    pendingToken_ = decoder_.Request(document_.Path(), index, view_.MaxBitmapSize());

    if (pendingToken_ == 0)
    {
        // Kein Thread -- dann bliebe das Fenster ohne Bild stehen. Das ist ein
        // Fall, der nur bei einem fehlgeschlagenen Start eintritt.
        MessageBoxW(hwnd_, L"Die Dekodierung im Hintergrund steht nicht zur Verfügung.",
                    kAppTitle, MB_ICONWARNING | MB_OK);
        return;
    }

    SetTimer(hwnd_, kLoadingTimer, kLoadingDelayMs, nullptr);
    RefreshCursor();
    UpdateTitle();
    UpdateToolbarState();
}

// Windows bestimmt den Zeiger erst bei der nächsten Mausbewegung neu. Wer per
// Tastatur blättert, bewegt sie aber nicht -- also wird die Frage einmal von
// Hand gestellt.
void MainWindow::RefreshCursor()
{
    POINT pt{};
    RECT rc{};
    if (GetCursorPos(&pt) && GetWindowRect(hwnd_, &rc) && PtInRect(&rc, pt))
    {
        SendMessageW(hwnd_, WM_SETCURSOR, reinterpret_cast<WPARAM>(hwnd_),
                     MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    }
}

void MainWindow::CancelDecode()
{
    const bool wasPending = pendingToken_ != 0;
    pendingToken_ = 0;
    loadingShown_ = false;
    KillTimer(hwnd_, kLoadingTimer);
    if (wasPending)
        RefreshCursor();
}

// Die Startdrehung einer frisch geöffneten Datei. Sie wird genau einmal
// eingelöst: eine Drehung von Hand soll den Seitenwechsel überdauern, und das
// ginge nicht, wenn jede Seite den EXIF-Wert erneut durchsetzte.
void MainWindow::ApplyPendingRotation()
{
    if (pendingRotation_ < 0)
        return;

    view_.View().SetRotation(pendingRotation_);
    pendingRotation_ = -1;
}

// Die Frist ist um und das Bild ist noch nicht da: jetzt wird der Ladezustand
// sichtbar. Das vorige Bild stehenzulassen wäre hier die schlechtere Wahl --
// es gehört zu einer anderen Datei, und Maßstab und Drehung in der Leiste
// bezögen sich auf etwas, das gar nicht mehr gemeint ist.
void MainWindow::OnLoadingDelay()
{
    KillTimer(hwnd_, kLoadingTimer);
    if (pendingToken_ == 0)
        return;

    loadingShown_ = true;
    view_.ClearImage();
    UpdateTitle();
    ApplyButtonStates();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::OnDecodeReady()
{
    DecodeResult result;
    if (!decoder_.Take(result))
        return;

    // Überholt: während dieses Bild entstand, ist ein neuerer Auftrag
    // ergangen. Es anzuzeigen hieße, kurz das falsche Bild zu zeigen.
    if (result.token == 0 || result.token != pendingToken_)
        return;

    CancelDecode();
    ApplyPendingRotation();

    std::wstring error = result.error;
    if (error.empty() && result.bitmap)
    {
        const HRESULT hr = view_.SetImage(result.bitmap.Get(), result.sourceWidth,
                                          result.sourceHeight);
        if (FAILED(hr))
            error = L"Bild konnte nicht dargestellt werden.\n\n" + FormatHResult(hr);
    }

    if (!error.empty())
    {
        // Die Seite bleibt die angesteuerte, die Fläche aber leer: so steht im
        // Titel, welche Seite gemeint war, und man sieht, dass sie fehlt.
        view_.ClearImage();
    }

    UpdateTitle();
    UpdateToolbarState();

    // Erst nach UpdateToolbarState: welche Knöpfe die Leiste zeigt, entscheidet
    // sich dort, und davon hängt ab, wie schmal das Fenster werden darf.
    FitWindowToImage();
    InvalidateRect(hwnd_, nullptr, FALSE);

    if (!error.empty())
        MessageBoxW(hwnd_, error.c_str(), kAppTitle, MB_ICONWARNING | MB_OK);
}

// Nur die Leinwand tauschen. Das läuft im Takt der Wiedergabe und fasst
// deshalb weder Titel noch Leiste an -- beides jedes Einzelbild neu zu setzen
// hieße, die Kurzhilfen zehnmal je Sekunde neu anzumelden.
bool MainWindow::ShowAnimationFrame(UINT index, std::wstring& error)
{
    ComPtr<IWICBitmapSource> frame = animator_.Compose(index, error);
    if (!frame)
        return false;

    const HRESULT hr = view_.UpdateImage(frame.Get());
    if (FAILED(hr))
    {
        error = L"Einzelbild konnte nicht dargestellt werden.\n\n" + FormatHResult(hr);
        return false;
    }

    frameIndex_ = index;
    return true;
}

void MainWindow::StepFrame(int delta)
{
    if (!document_.IsOpen() || document_.FrameCount() <= 1)
        return;

    const int count = static_cast<int>(document_.FrameCount());
    int target = 0;
    if (animator_.IsActive())
    {
        // Eine Animation hat kein Ende: hinter dem letzten Einzelbild geht es
        // vorn weiter, genau wie beim Abspielen. Seiten eines Dokuments werden
        // dagegen begrenzt, damit man das Ende überhaupt bemerkt.
        target = ((static_cast<int>(frameIndex_) + delta) % count + count) % count;
    }
    else
    {
        target = std::clamp(static_cast<int>(frameIndex_) + delta, 0, count - 1);
    }

    if (target != static_cast<int>(frameIndex_))
        ShowFrame(static_cast<UINT>(target));
}

// Anders als die Wiedergabe läuft der Ordner nicht um: am Ende bleibt es
// stehen, und der graue Knopf sagt, dass dort Schluss ist. Ohne Zähler im
// Titel wäre das sonst die einzige Stelle, an der man überhaupt merkt, wo man
// sich im Ordner befindet.
void MainWindow::StepFile(int delta)
{
    for (;;)
    {
        const std::wstring next = folder_.Neighbour(delta);
        if (next.empty())
            return;

        // Die Liste ist eine Momentaufnahme. Ist die Datei seither gelöscht
        // oder umbenannt worden, wird sie stillschweigend übergangen -- eine
        // Meldung über etwas, das der Benutzer selbst weggeräumt hat, hilft
        // ihm nicht weiter. Ein tatsächlich kaputtes Bild meldet dagegen
        // OpenFile, denn davon will man wissen.
        if (PathFileExistsW(next.c_str()))
        {
            OpenFile(next);
            return;
        }
        folder_.Drop(next);
    }
}

void MainWindow::OnDropFiles(HDROP drop)
{
    // Werden mehrere Dateien fallengelassen, wird die erste angezeigt. Die
    // übrigen liegen im selben Ordner und stehen damit ohnehin schon auf den
    // Pfeiltasten.
    const UINT length = DragQueryFileW(drop, 0, nullptr, 0);
    if (length > 0)
    {
        std::wstring path(length + 1, L'\0');
        if (DragQueryFileW(drop, 0, path.data(), length + 1) > 0)
        {
            path.resize(length);
            SetForegroundWindow(hwnd_);
            OpenFile(path);
        }
    }
    DragFinish(drop);
}

void MainWindow::StartPlayback()
{
    if (!animator_.IsActive() || playing_ || minimized_)
        return;

    playing_ = true;
    toolbar_.SetPlaying(true);
    nextDueTicks_ = QpcNow();
    ScheduleNextFrame();
}

void MainWindow::StopPlayback()
{
    if (!playing_)
        return;

    playing_ = false;
    toolbar_.SetPlaying(false);
    KillTimer(hwnd_, kAnimationTimer);
}

void MainWindow::TogglePlayback()
{
    if (!animator_.IsActive())
        return;

    if (playing_)
    {
        StopPlayback();
    }
    else
    {
        // Von Hand gestartet fängt auch die Zählung der Wiederholungen neu an.
        loopsDone_ = 0;

        // Steht das letzte Einzelbild, fängt die Wiedergabe wieder vorn an --
        // so wie ein Abspielgerät auch. Ohne das zählte der Umbruch beim
        // ersten Takt sofort als vollendeter Durchlauf, und ein GIF mit zwei
        // vorgesehenen Wiederholungen liefe nach dem Neustart nur einmal.
        if (frameIndex_ + 1 >= animator_.FrameCount())
        {
            std::wstring error;
            if (!ShowAnimationFrame(0, error))
            {
                MessageBoxW(hwnd_, error.c_str(), kAppTitle, MB_ICONWARNING | MB_OK);
                return;
            }
        }
        StartPlayback();
    }

    ApplyButtonStates();
    UpdateTitle();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::ScheduleNextFrame()
{
    if (!playing_ || !animator_.IsActive())
        return;

    const LONGLONG now = QpcNow();
    const LONGLONG step =
        static_cast<LONGLONG>(animator_.DelayMs(frameIndex_)) * qpcFrequency_ / 1000;

    nextDueTicks_ += step;

    // Zurückgefallen -- das Fenster wurde gezogen, das System war beschäftigt.
    // Dann wird der Takt neu angesetzt, statt die versäumten Einzelbilder im
    // Schnelldurchlauf nachzuholen; das sähe aus wie ein Ruckler mit Anlauf.
    if (nextDueTicks_ <= now)
        nextDueTicks_ = now + step;

    const LONGLONG milliseconds = (nextDueTicks_ - now) * 1000 / qpcFrequency_;
    SetTimer(hwnd_, kAnimationTimer,
             static_cast<UINT>(std::max<LONGLONG>(milliseconds, 1)), nullptr);
}

void MainWindow::OnAnimationTick()
{
    if (!playing_ || !animator_.IsActive())
        return;

    const UINT count = animator_.FrameCount();
    UINT next = frameIndex_ + 1;
    if (next >= count)
    {
        next = 0;
        ++loopsDone_;

        const UINT limit = animator_.LoopCount();
        if (limit != 0 && loopsDone_ >= limit)
        {
            // Die vorgesehenen Wiederholungen sind durch: auf dem letzten
            // Einzelbild stehenbleiben, so wie es Browser auch halten.
            StopPlayback();
            ApplyButtonStates();
            UpdateTitle();
            InvalidateToolbar();
            return;
        }
    }

    std::wstring error;
    if (!ShowAnimationFrame(next, error))
    {
        // Ein Einzelbild, das sich nicht komponieren lässt, scheiterte bei
        // jedem weiteren Takt erneut. Also anhalten und einmal Bescheid sagen.
        StopPlayback();
        ApplyButtonStates();
        UpdateTitle();
        InvalidateRect(hwnd_, nullptr, FALSE);
        MessageBoxW(hwnd_, error.c_str(), kAppTitle, MB_ICONWARNING | MB_OK);
        return;
    }

    InvalidateRect(hwnd_, nullptr, FALSE);
    ScheduleNextFrame();
}

void MainWindow::UpdateTitle()
{
    if (!document_.IsOpen())
    {
        SetWindowTextW(hwnd_, kAppTitle);
        return;
    }

    std::wstring name = document_.Path();
    if (const wchar_t* file = PathFindFileNameW(name.c_str()))
        name = file;

    std::wstring title = name;
    if (animator_.IsActive())
    {
        // Während der Wiedergabe bliebe die Zählung ohnehin unlesbar und
        // ließe Titel und Taskleiste flimmern; sie erscheint erst im Halt.
        if (!playing_)
        {
            title += L"  [Bild " + std::to_wstring(frameIndex_ + 1) + L"/" +
                     std::to_wstring(animator_.FrameCount()) + L"]";
        }
    }
    else if (document_.FrameCount() > 1)
    {
        title += L"  [Seite " + std::to_wstring(frameIndex_ + 1) + L"/" +
                 std::to_wstring(document_.FrameCount()) + L"]";
    }
    if (loadingShown_)
    {
        title += L"  wird geladen ...";
    }
    else if (view_.HasImage() && pendingToken_ == 0)
    {
        // Ohne Statusleiste ist der Titel der einzige Ort, an dem der Maßstab
        // ablesbar ist -- und beim Zoomen will man ihn wissen. Während ein
        // Bild unterwegs ist, bleibt er fort: er gehörte noch zum vorigen und
        // stünde dann neben dessen Namen schon nicht mehr.
        const int percent = static_cast<int>(std::lround(view_.View().Scale() * 100.0f));
        title += L"  " + std::to_wstring(percent) + L" %";
    }
    title += L" - ";
    title += kAppTitle;

    SetWindowTextW(hwnd_, title.c_str());
}

int MainWindow::RunMessageLoop()
{
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
