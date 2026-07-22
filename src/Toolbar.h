#pragma once

#include "Common.h"

#include <d2d1.h>

#include <vector>

enum class ToolbarCommand
{
    None,
    PreviousFile,
    NextFile,
    PreviousPage,
    PlayPause,
    NextPage,
    ZoomOut,
    ZoomIn,
    FitToWindow,
    ActualSize,
    RotateLeft,
    RotateRight,
    Print,
    Fullscreen,
};

struct ToolbarButton
{
    ToolbarCommand command;
    const wchar_t* tooltip;
    bool groupStart;      // davor eine Trennlinie, sofern links etwas steht
    bool mirrored;        // Icon horizontal gespiegelt zeichnen
    bool visible;
    bool enabled;
    D2D1_RECT_F rect;     // von Layout() gefuellt, in Pixeln
};

// Flache Leiste am unteren Rand. Die Icons sind Pfadgeometrien statt Bitmaps,
// damit sie auf jeder DPI-Stufe scharf bleiben, ohne mehrere Groessen zu pflegen.
class Toolbar
{
public:
    Toolbar();

    HRESULT CreateResources(ID2D1Factory* factory);
    void DiscardDeviceResources();   // nur die vom Render-Target abhaengigen

    void SetDpiScale(float scale);
    float Height() const;

    void SetVisible(ToolbarCommand command, bool visible);
    void SetEnabled(ToolbarCommand command, bool enabled);
    bool IsEnabled(ToolbarCommand command) const;

    // Bestimmt, ob der Wiedergabeknopf die Pausen- oder die Abspielmarke zeigt.
    void SetPlaying(bool playing) { playing_ = playing; }

    // Desgleichen fuer das Vollbild: die Pfeile zeigen nach aussen oder innen.
    void SetFullscreen(bool fullscreen) { fullscreen_ = fullscreen; }

    void Layout(float clientWidth, float clientHeight);
    void Draw(ID2D1RenderTarget* target);

    ToolbarCommand HitTest(float x, float y) const;
    bool SetHover(ToolbarCommand command);      // true, wenn sich die Anzeige aendert
    bool SetPressed(ToolbarCommand command);
    ToolbarCommand Pressed() const { return pressed_; }

    D2D1_RECT_F StripRect() const { return strip_; }
    const std::vector<ToolbarButton>& Buttons() const { return buttons_; }

private:
    const ToolbarButton* Find(ToolbarCommand command) const;
    ToolbarButton* Find(ToolbarCommand command);
    void DrawIcon(ID2D1RenderTarget* target, const ToolbarButton& button, ID2D1Brush* brush);
    void DrawMagnifier(ID2D1RenderTarget* target, ID2D1Brush* brush, bool plus);
    void DrawFitMarks(ID2D1RenderTarget* target, ID2D1Brush* brush);
    void DrawOneToOne(ID2D1RenderTarget* target, ID2D1Brush* brush);
    void DrawPlayPause(ID2D1RenderTarget* target, ID2D1Brush* brush);
    void DrawFullscreen(ID2D1RenderTarget* target, ID2D1Brush* brush);

    std::vector<ToolbarButton> buttons_;
    std::vector<float> separators_;   // x-Positionen der Trennlinien
    ToolbarCommand hover_ = ToolbarCommand::None;
    ToolbarCommand pressed_ = ToolbarCommand::None;
    float dpiScale_ = 1.0f;
    bool playing_ = false;
    bool fullscreen_ = false;
    D2D1_RECT_F strip_{};

    ComPtr<ID2D1PathGeometry> triangle_;   // zeigt nach rechts
    ComPtr<ID2D1PathGeometry> chevron_;    // desgleichen, aber nur als Strich
    ComPtr<ID2D1PathGeometry> arcBody_;    // Rotationsbogen im Uhrzeigersinn
    ComPtr<ID2D1PathGeometry> arcHead_;    // zugehoerige Pfeilspitze
    ComPtr<ID2D1PathGeometry> printer_;    // Kasten mit Blatt hinein und heraus
    ComPtr<ID2D1SolidColorBrush> brush_;
};
