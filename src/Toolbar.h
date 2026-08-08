// Bildanzeige -- Copyright (C) 2026 EvilKnievel89
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Freie Software unter der GNU GPL, Version 3 oder einer späteren: weitergeben
// und verändern ist erlaubt, solange Quelltext und Lizenz mitgehen. Ohne jede
// Gewährleistung. Wortlaut in LICENSE, Erklärung in LIZENZ.md.

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
    Settings,
};

struct ToolbarButton
{
    ToolbarCommand command;
    const wchar_t* tooltip;
    bool groupStart;      // davor eine Trennlinie, sofern links etwas steht
    bool mirrored;        // Icon horizontal gespiegelt zeichnen
    bool visible;
    bool enabled;
    D2D1_RECT_F rect;     // von Layout() gefüllt, in Pixeln

    // Steht am rechten Rand statt in der mittigen Gruppe. Der Wert kommt
    // zuletzt, damit die Zeilen der Leiste, für die er nicht gilt, ihn gar
    // nicht erst aufführen müssen.
    bool trailing = false;
};

// Flache Leiste am unteren Rand. Die Icons sind Pfadgeometrien statt Bitmaps,
// damit sie auf jeder DPI-Stufe scharf bleiben, ohne mehrere Größen zu pflegen.
class Toolbar
{
public:
    Toolbar();

    HRESULT CreateResources(ID2D1Factory* factory);
    void DiscardDeviceResources();   // nur die vom Render-Target abhängigen

    void SetDpiScale(float scale);
    float Height() const;

    // Breite, unter die das Fenster nicht schrumpfen darf: darunter stünden die
    // äußeren Knöpfe über den Rand hinaus und wären nicht mehr zu treffen.
    // Hängt davon ab, welche Knöpfe gerade sichtbar sind -- und davon, dass die
    // mittige Gruppe dem rechten Block ausweichen können muss.
    float MinimumWidth() const;

    void SetVisible(ToolbarCommand command, bool visible);
    void SetEnabled(ToolbarCommand command, bool enabled);
    bool IsEnabled(ToolbarCommand command) const;

    // Bestimmt, ob der Wiedergabeknopf die Pausen- oder die Abspielmarke zeigt.
    void SetPlaying(bool playing) { playing_ = playing; }

    // Desgleichen für das Vollbild: die Pfeile zeigen nach außen oder innen.
    void SetFullscreen(bool fullscreen) { fullscreen_ = fullscreen; }

    void Layout(float clientWidth, float clientHeight);
    void Draw(ID2D1RenderTarget* target);

    ToolbarCommand HitTest(float x, float y) const;
    bool SetHover(ToolbarCommand command);      // true, wenn sich die Anzeige ändert
    bool SetPressed(ToolbarCommand command);
    ToolbarCommand Pressed() const { return pressed_; }

    D2D1_RECT_F StripRect() const { return strip_; }
    const std::vector<ToolbarButton>& Buttons() const { return buttons_; }

private:
    float ButtonsWidth() const;    // nur die mittige Gruppe
    float TrailingWidth() const;   // nur der rechte Block
    const ToolbarButton* Find(ToolbarCommand command) const;
    ToolbarButton* Find(ToolbarCommand command);
    void DrawIcon(ID2D1RenderTarget* target, const ToolbarButton& button, ID2D1Brush* brush);
    void DrawMagnifier(ID2D1RenderTarget* target, ID2D1Brush* brush, bool plus);
    void DrawFitMarks(ID2D1RenderTarget* target, ID2D1Brush* brush);
    void DrawOneToOne(ID2D1RenderTarget* target, ID2D1Brush* brush);
    void DrawPlayPause(ID2D1RenderTarget* target, ID2D1Brush* brush);
    void DrawFullscreen(ID2D1RenderTarget* target, ID2D1Brush* brush);
    void PlaceTrailing(float clientWidth, float y);

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
    ComPtr<ID2D1PathGeometry> arcHead_;    // zugehörige Pfeilspitze
    ComPtr<ID2D1PathGeometry> printer_;    // Kasten mit Blatt hinein und heraus
    ComPtr<ID2D1PathGeometry> gear_;       // Zahnrad mit Bohrung, gefüllt
    ComPtr<ID2D1SolidColorBrush> brush_;
};
