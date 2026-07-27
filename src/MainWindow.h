#pragma once

#include "Common.h"
#include "DecodeWorker.h"
#include "FolderNavigator.h"
#include "GifAnimator.h"
#include "ImageDocument.h"
#include "RenderView.h"
#include "Toolbar.h"

#include <shellapi.h>   // HDROP
#include <wincodec.h>

#include <string>

class MainWindow
{
public:
    bool Create(HINSTANCE instance, int showCmd);
    bool OpenFile(const std::wstring& path);
    int RunMessageLoop();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    bool ShowFrame(UINT index);
    bool ShowAnimationFrame(UINT index, std::wstring& error);
    void RequestFrame(UINT index);
    void CancelDecode();
    void ApplyPendingRotation();
    void OnDecodeReady();
    void OnLoadingDelay();
    void StepFrame(int delta);
    void StepFile(int delta);
    void OnDropFiles(HDROP drop);
    void Execute(ToolbarCommand command);
    void PrintCurrent();

    void StartPlayback();
    void StopPlayback();
    void TogglePlayback();
    void ScheduleNextFrame();
    void OnAnimationTick();

    void OnPaint();
    void OnKeyDown(WPARAM key);
    void OnMouseMove(float x, float y);
    void OnLeftButtonDown(float x, float y);
    void OnLeftButtonUp(float x, float y);
    void OnLeftButtonDoubleClick(float x, float y);
    void OnMouseWheel(int delta, POINT screen);
    void OnMouseLeave();
    void OnCaptureChanged();
    bool OnSetCursor();
    void RefreshCursor();
    void OnDpiChanged(UINT dpi, const RECT* suggested);
    void FitWindowToImage();
    RECT FrameOverhang() const;
    void ToggleFullscreen();
    void RecreateDeviceResources();
    void UpdateTitle();

    void Zoom(float factor);
    void EndPan();
    void AfterViewChange();

    void ApplyDpi(UINT dpi);
    void ApplyIcons(UINT dpi);
    void LayoutToolbar();
    void UpdateToolbarState();
    void ApplyButtonStates();
    void InvalidateToolbar();
    void CreateTooltip();
    void UpdateTooltipRects();

    HWND hwnd_ = nullptr;
    HWND tooltip_ = nullptr;

    // Anwendungssymbol in den beiden Größen, die das Fenster selbst hält
    // (Titelleiste und Taskleiste). Sie hängen an der DPI-Stufe und werden
    // beim Wechsel neu geladen -- deshalb liegen sie hier und nicht als
    // gemeinsames Klassensymbol.
    HICON iconLarge_ = nullptr;
    HICON iconSmall_ = nullptr;
    ComPtr<IWICImagingFactory> wic_;
    ImageDocument document_;
    FolderNavigator folder_;
    GifAnimator animator_;
    DecodeWorker decoder_;
    RenderView view_;
    Toolbar toolbar_;
    UINT frameIndex_ = 0;
    bool trackingMouse_ = false;
    bool panning_ = false;
    POINT panAnchor_{};

    // Marke des Auftrags, auf den gewartet wird (0 = keiner). Ergebnisse mit
    // einer anderen Marke sind überholt und werden verworfen.
    unsigned long long pendingToken_ = 0;

    // Erst nach einer kurzen Frist wird die Anzeige geleert. Bei einem
    // gewöhnlichen Foto ist das Bild lange vorher da, und ein Aufblitzen des
    // leeren Hintergrunds bei jedem Tastendruck wäre schlimmer als das Warten.
    bool loadingShown_ = false;

    // Startdrehung aus EXIF, die noch auf ihr Bild wartet (-1 = keine).
    int pendingRotation_ = -1;

    bool fullscreen_ = false;
    WINDOWPLACEMENT placement_{};
    LONG_PTR savedStyle_ = 0;

    bool playing_ = false;
    bool minimized_ = false;
    UINT loopsDone_ = 0;
    LONGLONG qpcFrequency_ = 1;

    // Absoluter Zeitpunkt des nächsten Einzelbildes. Fällig gewordene Zeiten
    // werden aufsummiert, nicht aneinandergereiht -- sonst liefe die Wiedergabe
    // um die Auflösung des Timers (~15,6 ms) je Bild nach.
    LONGLONG nextDueTicks_ = 0;
};
