#include "MainWindow.h"

#include "PrintPreview.h"
#include "Resource.h"

#include <commctrl.h>
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

    // Meldung des Hintergrund-Threads, dass ein Auftrag fertig ist.
    constexpr UINT kDecodeReady = WM_APP + 1;

    // So lange bleibt das vorige Bild stehen, bevor der Ladezustand sichtbar
    // wird. Darunter liegt praktisch jedes Foto; erst was laenger braucht,
    // soll ueberhaupt als Warten in Erscheinung treten.
    constexpr UINT kLoadingDelayMs = 150;

    // Fenstergroesse bei 96 dpi; auf anderen Stufen entsprechend vervielfacht.
    constexpr int kInitialClientWidth = 1000;
    constexpr int kInitialClientHeight = 660;

    LONGLONG QpcNow()
    {
        LARGE_INTEGER counter{};
        QueryPerformanceCounter(&counter);
        return counter.QuadPart;
    }

    // Von der Kommandozeile kommt der Pfad oft relativ ("testdata\bild.png").
    // Der Ordner der Nachbardateien liesse sich daraus nicht bestimmen, sobald
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
        MessageBoxW(nullptr, (L"WIC ist nicht verfuegbar.\n\n" + FormatHResult(hr)).c_str(),
                    kAppTitle, MB_ICONERROR | MB_OK);
        return false;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = &MainWindow::WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;   // Direct2D zeichnet die gesamte Flaeche.
    // Symbol der Fensterklasse: was zu sehen ist, bis das Fenster in WM_CREATE
    // seine eigenen, zur DPI-Stufe passenden Symbole setzt. Das kleine ist
    // eigens angegeben, weil Windows es sonst aus dem grossen herunterrechnet
    // -- 32 auf 16 verschmiert genau die feinen Striche, fuer die die .ico ein
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

    // Die Groesse wird erst jetzt gesetzt: vorher steht nicht fest, auf welchem
    // Bildschirm das Fenster landet. Bei 150 % waere ein fest in Pixeln
    // angegebenes Fenster nur zwei Drittel so gross wie gemeint. Der Rahmen
    // waechst dabei nicht im selben Verhaeltnis wie die Bildflaeche, deshalb
    // rechnet ihn AdjustWindowRectExForDpi zur gewuenschten Flaeche hinzu.
    const UINT dpi = GetDpiForWindow(hwnd_);
    const float dpiScale = static_cast<float>(dpi > 0 ? dpi : 96) / 96.0f;
    RECT desired = { 0, 0, static_cast<LONG>(kInitialClientWidth * dpiScale),
                     static_cast<LONG>(kInitialClientHeight * dpiScale) };
    if (AdjustWindowRectExForDpi(&desired, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_ACCEPTFILES, dpi))
    {
        SetWindowPos(hwnd_, nullptr, 0, 0, desired.right - desired.left,
                     desired.bottom - desired.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
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
                // Zeit im Symbolzustand als Rueckstand nachzuholen.
                nextDueTicks_ = QpcNow();
                ScheduleNextFrame();
            }
        }
        view_.Resize(LOWORD(lParam), HIWORD(lParam));
        LayoutToolbar();
        // Der Einpass-Massstab haengt am Fenster: nach dem Groessenaendern kann
        // sich entscheiden, ob sich ueberhaupt noch herauszoomen laesst.
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
        return 1;   // Verhindert Flackern; Direct2D fuellt ohnehin alles.

    case WM_DPICHANGED:
        OnDpiChanged(LOWORD(wParam), reinterpret_cast<const RECT*>(lParam));
        return 0;

    case WM_DISPLAYCHANGE:
        // Aufloesung oder Anordnung der Bildschirme hat gewechselt. Das
        // Render-Target haengt an der alten Ausgabe; es neu aufzubauen kostet
        // hier nichts und erspart ein schwarzes Fenster.
        RecreateDeviceResources();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;

    case WM_KEYDOWN:
        OnKeyDown(wParam);
        return 0;

    case WM_DROPFILES:
        OnDropFiles(reinterpret_cast<HDROP>(wParam));
        return 0;

    case WM_DESTROY:
        StopPlayback();
        // Erst den Thread einholen, dann die Ressourcen abraeumen: er haelt
        // waehrend eines Auftrags eine eigene WIC-Fabrik in der Hand.
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
        // Geraeteressourcen verloren (GPU-Reset, Treiberwechsel). Neu aufbauen
        // und einen weiteren Durchgang anfordern -- sonst bleibt das Fenster schwarz.
        RecreateDeviceResources();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    EndPaint(hwnd_, &ps);
}

// Der Wiederaufbau nach einem Geraeteverlust.
//
// Mit dem Geraet ist auch die Bitmap fort. RenderView haelt die WIC-Quelle
// nicht vor -- bei einem grossen Bild waere das dauerhaft eine viertel
// Gigabyte fuer einen Fall, der jahrelang ausbleiben kann. Das Bild wird
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

    // Verschoben wird mit Strg und Pfeiltaste. Die blossen Pfeiltasten gehoeren
    // dem Bildwechsel im Ordner: dieselbe Taste je nach Zoomstufe verschieden
    // zu belegen waere fuer den Benutzer nicht absehbar.
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
    // Esc verlaesst zuerst das Vollbild. Wer es geoeffnet hat, erwartet dort
    // den Rueckweg -- das Fenster gleich ganz zu schliessen waere eine boese
    // Ueberraschung, weil im Vollbild kein Rahmen daran erinnert, dass noch ein
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
    // verschieben, laeuft der Tastendruck hier durch -- dann aber ins Leere,
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
        // Ohne WM_MOUSELEAVE bliebe die Hervorhebung haengen, wenn der Zeiger
        // das Fenster verlaesst, ohne vorher die Leiste zu passieren.
        TRACKMOUSEEVENT track{};
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = hwnd_;
        if (TrackMouseEvent(&track))
            trackingMouse_ = true;
    }

    if (panning_)
    {
        // Verschieben zaehlt in ganzen Bildschirmpixeln, damit sich der Zeiger
        // nicht durch aufsummierte Rundungsreste vom Bild loest.
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

    // Nur die Hervorhebung zuruecksetzen. Einen laufenden Tastendruck hier zu
    // verwerfen wuerde ihn verschlucken, sobald der Zeiger kurz das Fenster
    // verlaesst -- der Abbruch wird in OnLeftButtonUp anhand der Zeigerposition
    // entschieden, der unfreiwillige Verlust ueber WM_CAPTURECHANGED.
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
    // Waehrend ein Bild geholt wird, sagt der Zeiger es als erster -- er ist
    // die einzige Rueckmeldung, die schon vor der Frist des Ladezustands da ist.
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
        return false;   // Klassenzeiger genuegt

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

    // Ausserhalb der Leiste: den Ausschnitt ziehen, sofern es einen gibt.
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

    // Zuerst sichern: ReleaseCapture loest WM_CAPTURECHANGED aus, was den
    // Zustand noch vor der Auswertung zuruecksetzt.
    const ToolbarCommand pressed = toolbar_.Pressed();
    if (pressed == ToolbarCommand::None)
        return;

    ReleaseCapture();
    InvalidateToolbar();

    // Nur ausloesen, wenn der Zeiger beim Loslassen noch auf demselben Knopf
    // steht -- so laesst sich ein versehentlicher Klick durch Wegziehen abbrechen.
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
    // verloren -- wer zweimal rasch auf "drehen" tippt, kaeme nur auf 90 statt
    // 180 Grad.
    if (toolbar_.HitTest(x, y) != ToolbarCommand::None)
    {
        OnLeftButtonDown(x, y);
        return;
    }

    // Auf der Bildflaeche schaltet er das Vollbild -- ueber der Leiste nicht,
    // dort waere er nur das versehentliche Nachfassen auf einem grauen Knopf.
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

    // Ueber der Leiste gibt es keinen Bildpunkt, an dem sich der Zoom
    // festmachen liesse; der Anker wandert dann an den unteren Bildrand.
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

    case ToolbarCommand::RotateLeft:
        view_.View().Rotate(-1);
        AfterViewChange();
        break;

    case ToolbarCommand::RotateRight:
        view_.View().Rotate(+1);
        AfterViewChange();
        break;

    case ToolbarCommand::Print:
        PrintCurrent();
        break;

    case ToolbarCommand::Fullscreen:
        ToggleFullscreen();
        break;

    default:
        break;
    }
}

// Gedruckt wird, was zu sehen ist: die gezeigte Seite in der Drehung der
// Anzeige. Zoom und Ausschnitt bleiben aussen vor -- sie sind die Lupe, mit der
// man das Bild betrachtet, nicht das Bild.
//
// Der Weg fuehrt ueber die Seitenansicht und nicht geradewegs in den
// Druckdialog. Windows 11 hat den Dialog gegen einen eigenen ausgetauscht, und
// dessen Vorschaufeld bleibt bei jeder Win32-Anwendung leer ("Diese App
// unterstuetzt keine Seitenansicht") -- es will die Seiten schon haben, bevor
// gedruckt wird, und eine GDI-Anwendung erzeugt sie erst danach. Die eigene
// Ansicht davor schliesst die Luecke.
void MainWindow::PrintCurrent()
{
    if (!document_.IsOpen())
        return;

    // Vor dem Dialog anhalten. Er ist modal, aber der Zeitgeber schlaegt in
    // seiner Nachrichtenschleife weiter -- die Seite waere unter dem Auftrag
    // fortgewandert, und gedruckt kaeme etwas anderes heraus als das, was beim
    // Druecken zu sehen war.
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

// Das Vollbild nimmt dem Fenster nur den Rahmen und legt es ueber den ganzen
// Bildschirm. Die Leiste bleibt stehen: sie einzublenden, sobald sich die Maus
// regt, hiesse einen zweiten Satz Regeln fuer Sichtbarkeit, Zeitgeber und
// Treffpruefung zu fuehren -- und ohne Rahmen und Menue waere sie der einzige
// sichtbare Rueckweg.
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

        // rcMonitor, nicht rcWork: die Taskleiste wird mit ueberdeckt.
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
        // maximiert war -- eine gemerkte Rechteckgroesse koennte das nicht.
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
    // Titelleiste und Taskleiste zeigen das Symbol in der Groesse, die zur
    // Stufe des jeweiligen Bildschirms gehoert: bei 150 % also 24 statt 16
    // Punkte. Bekaeme Windows nur das 16er, zoege es das auf 24 hoch. Die .ico
    // hat 24 fertig dabei -- es genuegt, die Groesse zu erfragen und das
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
        // irgendetwas abzubrechen -- es sieht nur eine Spur unschaerfer aus.
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

    // Die Schrittknoepfe verschwinden ganz, wenn es nichts zu blaettern gibt --
    // bei einem gewoehnlichen Foto bleibt die Leiste dadurch aufgeraeumt.
    toolbar_.SetVisible(ToolbarCommand::PreviousPage, multiPage);
    toolbar_.SetVisible(ToolbarCommand::NextPage, multiPage);
    toolbar_.SetVisible(ToolbarCommand::PlayPause, animator_.IsActive());

    ApplyButtonStates();
    LayoutToolbar();
    InvalidateToolbar();
}

// Nur die Verfuegbarkeit, ohne Neuberechnung des Layouts: das laeuft bei jeder
// Rad-Rastung durch und soll nicht jedes Mal die Kurzhilfen neu anmelden.
void MainWindow::ApplyButtonStates()
{
    const bool multiPage = document_.IsOpen() && document_.FrameCount() > 1;
    const bool hasImage = view_.HasImage();
    const ViewState& state = view_.View();

    toolbar_.SetEnabled(ToolbarCommand::PreviousFile, folder_.HasNeighbour(-1));
    toolbar_.SetEnabled(ToolbarCommand::NextFile, folder_.HasNeighbour(+1));

    // Bei einer Animation sind die Schrittknoepfe der Einzelschritt und deshalb
    // nur im Halt verfuegbar. Sie werden dabei bewusst nicht aus- und wieder
    // eingeblendet: die Leiste ist mittig gesetzt, das Ein- und Ausblenden
    // rueckte den Wiedergabeknopf unter dem Zeiger fort, kaum dass man ihn
    // getroffen hat.
    const bool animated = animator_.IsActive();
    toolbar_.SetEnabled(ToolbarCommand::PreviousPage,
                        animated ? !playing_ : (multiPage && frameIndex_ > 0));
    toolbar_.SetEnabled(ToolbarCommand::NextPage,
                        animated ? !playing_
                                 : (multiPage && frameIndex_ + 1 < document_.FrameCount()));
    toolbar_.SetEnabled(ToolbarCommand::PlayPause, animated);
    toolbar_.SetPlaying(playing_);

    // Graue Knoepfe sagen hier zugleich, wie das Bild gerade steht: "Einpassen"
    // ist grau, solange eingepasst ist, "Originalgroesse" bei 100 %.
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

    // Das Vollbild haengt am Fenster, nicht am Bild: es bleibt auch dann
    // erreichbar, wenn gerade nichts geladen ist.
    toolbar_.SetEnabled(ToolbarCommand::Fullscreen, true);
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
        // Unsichtbare Knoepfe bekommen ein leeres Rechteck, sonst zeigte die
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
// der Nachricht -- sie am Fenster zu erfragen waere ein Umweg ueber einen
// Zustand, den das System gerade erst mitgeteilt hat.
void MainWindow::OnDpiChanged(UINT dpi, const RECT* suggested)
{
    if (suggested != nullptr)
    {
        // Das Fenster folgt dem Vorschlag des Systems; die Bildflaeche rechnet
        // in Pixeln, es ist also nichts weiter umzurechnen.
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    ApplyDpi(dpi);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool MainWindow::OpenFile(const std::wstring& path)
{
    std::wstring full = MakeAbsolute(path);

    // Ein Ordner statt einer Datei -- so kommt es an, wenn einer auf das
    // Fenster gezogen oder auf der Kommandozeile uebergeben wird. Gemeint ist
    // dann das erste Bild darin.
    if (PathIsDirectoryW(full.c_str()))
    {
        const std::wstring first = folder_.FirstIn(wic_.Get(), full);
        if (first.empty())
        {
            MessageBoxW(hwnd_, L"Der Ordner enthaelt kein anzeigbares Bild.",
                        kAppTitle, MB_ICONWARNING | MB_OK);
            return false;
        }
        full = first;
    }

    // Auch bei einer Datei, die sich nicht oeffnen laesst: erst dadurch bleiben
    // die Nachbarn erreichbar, und man kommt ueber eine kaputte Datei hinweg,
    // statt in ihr steckenzubleiben.
    folder_.Track(wic_.Get(), full);

    // Vor dem Oeffnen anhalten: der laufende Takt griffe sonst waehrend des
    // Wechsels auf einen Decoder zu, den ImageDocument::Open gerade ersetzt.
    StopPlayback();
    animator_.Reset();
    loopsDone_ = 0;

    // Ein noch unterwegs befindliches Bild gehoert zur vorigen Datei. Ohne das
    // Verwerfen koennte es nach dem Wechsel eintreffen und das neue ueberschreiben.
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

    // Ein hochkant gehaltenes Foto steht quer in der Datei und traegt die
    // Drehung nur als Vermerk. Sie ist die Ausgangslage, auf die sich ein
    // spaeteres Drehen von Hand bezieht -- greifen darf sie aber erst, wenn das
    // neue Bild da ist, sonst kippte fuer einen Augenblick noch das vorige.
    pendingRotation_ = document_.OrientationQuarters();

    if (animator_.IsActive())
    {
        // Die Leinwand wird geleert, damit das erste Einzelbild als
        // Groessenwechsel durchgeht und eingepasst anfaengt -- auch dann, wenn
        // die vorige Datei zufaellig gleich gross war. Bei einem gewoehnlichen
        // Bild geschieht das von selbst, weil dort jedes Bild frisch eingepasst
        // wird; es hier ebenso zu leeren hiesse, bei jedem Tastendruck den
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
        // ShowFrame hat den Titel gesetzt, als noch nichts lief -- er truege
        // sonst die Bildzaehlung des Halts durch die ganze Wiedergabe.
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
        // Die Einzelbilder eines GIFs entstehen durch Uebereinanderlegen und
        // muessen im Takt der Wiedergabe bereitstehen. Sie sind klein genug,
        // dass sich der Umweg ueber den Hintergrund-Thread nicht lohnt -- er
        // braechte hier nur die Frage mit, was gilt, wenn der Takt schneller
        // schlaegt als die Antwort eintrifft.
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
// Tastendruck waehrend des Ladens noch von der alten Seite aus und bliebe
// wirkungslos.
void MainWindow::RequestFrame(UINT index)
{
    frameIndex_ = index;
    pendingToken_ = decoder_.Request(document_.Path(), index, view_.MaxBitmapSize());

    if (pendingToken_ == 0)
    {
        // Kein Thread -- dann bliebe das Fenster ohne Bild stehen. Das ist ein
        // Fall, der nur bei einem fehlgeschlagenen Start eintritt.
        MessageBoxW(hwnd_, L"Die Dekodierung im Hintergrund steht nicht zur Verfuegung.",
                    kAppTitle, MB_ICONWARNING | MB_OK);
        return;
    }

    SetTimer(hwnd_, kLoadingTimer, kLoadingDelayMs, nullptr);
    RefreshCursor();
    UpdateTitle();
    UpdateToolbarState();
}

// Windows bestimmt den Zeiger erst bei der naechsten Mausbewegung neu. Wer per
// Tastatur blaettert, bewegt sie aber nicht -- also wird die Frage einmal von
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

// Die Startdrehung einer frisch geoeffneten Datei. Sie wird genau einmal
// eingeloest: eine Drehung von Hand soll den Seitenwechsel ueberdauern, und das
// ginge nicht, wenn jede Seite den EXIF-Wert erneut durchsetzte.
void MainWindow::ApplyPendingRotation()
{
    if (pendingRotation_ < 0)
        return;

    view_.View().SetRotation(pendingRotation_);
    pendingRotation_ = -1;
}

// Die Frist ist um und das Bild ist noch nicht da: jetzt wird der Ladezustand
// sichtbar. Das vorige Bild stehenzulassen waere hier die schlechtere Wahl --
// es gehoert zu einer anderen Datei, und Massstab und Drehung in der Leiste
// bezoegen sich auf etwas, das gar nicht mehr gemeint ist.
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

    // Ueberholt: waehrend dieses Bild entstand, ist ein neuerer Auftrag
    // ergangen. Es anzuzeigen hiesse, kurz das falsche Bild zu zeigen.
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
        // Die Seite bleibt die angesteuerte, die Flaeche aber leer: so steht im
        // Titel, welche Seite gemeint war, und man sieht, dass sie fehlt.
        view_.ClearImage();
    }

    UpdateTitle();
    UpdateToolbarState();
    InvalidateRect(hwnd_, nullptr, FALSE);

    if (!error.empty())
        MessageBoxW(hwnd_, error.c_str(), kAppTitle, MB_ICONWARNING | MB_OK);
}

// Nur die Leinwand tauschen. Das laeuft im Takt der Wiedergabe und fasst
// deshalb weder Titel noch Leiste an -- beides jedes Einzelbild neu zu setzen
// hiesse, die Kurzhilfen zehnmal je Sekunde neu anzumelden.
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
        // dagegen begrenzt, damit man das Ende ueberhaupt bemerkt.
        target = ((static_cast<int>(frameIndex_) + delta) % count + count) % count;
    }
    else
    {
        target = std::clamp(static_cast<int>(frameIndex_) + delta, 0, count - 1);
    }

    if (target != static_cast<int>(frameIndex_))
        ShowFrame(static_cast<UINT>(target));
}

// Anders als die Wiedergabe laeuft der Ordner nicht um: am Ende bleibt es
// stehen, und der graue Knopf sagt, dass dort Schluss ist. Ohne Zaehler im
// Titel waere das sonst die einzige Stelle, an der man ueberhaupt merkt, wo man
// sich im Ordner befindet.
void MainWindow::StepFile(int delta)
{
    for (;;)
    {
        const std::wstring next = folder_.Neighbour(delta);
        if (next.empty())
            return;

        // Die Liste ist eine Momentaufnahme. Ist die Datei seither geloescht
        // oder umbenannt worden, wird sie stillschweigend uebergangen -- eine
        // Meldung ueber etwas, das der Benutzer selbst weggeraeumt hat, hilft
        // ihm nicht weiter. Ein tatsaechlich kaputtes Bild meldet dagegen
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
    // uebrigen liegen im selben Ordner und stehen damit ohnehin schon auf den
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
        // Von Hand gestartet faengt auch die Zaehlung der Wiederholungen neu an.
        loopsDone_ = 0;

        // Steht das letzte Einzelbild, faengt die Wiedergabe wieder vorn an --
        // so wie ein Abspielgeraet auch. Ohne das zaehlte der Umbruch beim
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

    // Zurueckgefallen -- das Fenster wurde gezogen, das System war beschaeftigt.
    // Dann wird der Takt neu angesetzt, statt die versaeumten Einzelbilder im
    // Schnelldurchlauf nachzuholen; das saehe aus wie ein Ruckler mit Anlauf.
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
        // Ein Einzelbild, das sich nicht komponieren laesst, scheiterte bei
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
        // Waehrend der Wiedergabe bliebe die Zaehlung ohnehin unlesbar und
        // liesse Titel und Taskleiste flimmern; sie erscheint erst im Halt.
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
        // Ohne Statusleiste ist der Titel der einzige Ort, an dem der Massstab
        // ablesbar ist -- und beim Zoomen will man ihn wissen. Waehrend ein
        // Bild unterwegs ist, bleibt er fort: er gehoerte noch zum vorigen und
        // stuende dann neben dessen Namen schon nicht mehr.
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
