// Bildanzeige -- Copyright (C) 2026 EvilKnievel89
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Freie Software unter der GNU GPL, Version 3 oder einer späteren: weitergeben
// und verändern ist erlaubt, solange Quelltext und Lizenz mitgehen. Ohne jede
// Gewährleistung. Wortlaut in LICENSE, Erklärung in LIZENZ.md.

#include "ViewState.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kMaxScale = 32.0f;
    constexpr float kEpsilon = 1.0001f;
}

void ViewState::SetImageSize(D2D1_SIZE_F size)
{
    image_ = size;
}

void ViewState::SetViewport(D2D1_SIZE_F size)
{
    viewport_ = size;
}

void ViewState::Reset()
{
    fit_ = true;
    scale_ = 1.0f;
    const D2D1_SIZE_F shown = Shown();
    center_ = D2D1::Point2F(shown.width * 0.5f, shown.height * 0.5f);
}

void ViewState::SetRotation(int quarters)
{
    rotation_ = ((quarters % 4) + 4) % 4;
}

void ViewState::Rotate(int quarters)
{
    rotation_ = ((rotation_ + quarters) % 4 + 4) % 4;

    // Der bisherige Ausschnitt ließe sich mitdrehen, ergäbe aber selten das,
    // was der Betrachter erwartet: nach dem Drehen sucht man den Bildmittelpunkt.
    const D2D1_SIZE_F shown = Shown();
    center_ = D2D1::Point2F(shown.width * 0.5f, shown.height * 0.5f);
}

bool ViewState::HasImage() const
{
    return image_.width > 0.0f && image_.height > 0.0f;
}

D2D1_SIZE_F ViewState::Shown() const
{
    return (rotation_ % 2) != 0 ? D2D1::SizeF(image_.height, image_.width) : image_;
}

float ViewState::FitScale() const
{
    const D2D1_SIZE_F shown = Shown();
    if (shown.width <= 0.0f || shown.height <= 0.0f ||
        viewport_.width <= 0.0f || viewport_.height <= 0.0f)
    {
        return 1.0f;
    }

    // Kleine Bilder bleiben in Originalgröße stehen, statt aufgeblasen zu
    // werden -- so verhielt sich auch die alte Bild- und Faxanzeige.
    return std::min(1.0f, std::min(viewport_.width / shown.width,
                                   viewport_.height / shown.height));
}

float ViewState::Scale() const
{
    return fit_ ? FitScale() : scale_;
}

D2D1_POINT_2F ViewState::ClampCenter(D2D1_POINT_2F center, float scale) const
{
    const D2D1_SIZE_F shown = Shown();

    // Passt das Bild in eine Richtung ohnehin hinein, wird es dort mittig
    // gesetzt; sonst wird so begrenzt, dass am Rand kein Streifen Hintergrund
    // auftaucht. Das hält das Bild beim Ziehen bündig am Fensterrand.
    if (shown.width * scale <= viewport_.width)
    {
        center.x = shown.width * 0.5f;
    }
    else
    {
        const float half = viewport_.width * 0.5f / scale;
        center.x = std::clamp(center.x, half, shown.width - half);
    }

    if (shown.height * scale <= viewport_.height)
    {
        center.y = shown.height * 0.5f;
    }
    else
    {
        const float half = viewport_.height * 0.5f / scale;
        center.y = std::clamp(center.y, half, shown.height - half);
    }

    return center;
}

D2D1_POINT_2F ViewState::Center() const
{
    const D2D1_SIZE_F shown = Shown();
    if (fit_)
        return D2D1::Point2F(shown.width * 0.5f, shown.height * 0.5f);
    return ClampCenter(center_, scale_);
}

D2D1_RECT_F ViewState::DestRect() const
{
    const D2D1_SIZE_F shown = Shown();
    if (shown.width <= 0.0f || shown.height <= 0.0f)
        return D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);

    const float scale = Scale();
    const D2D1_POINT_2F center = Center();
    const float left = viewport_.width * 0.5f - center.x * scale;
    const float top = viewport_.height * 0.5f - center.y * scale;

    return D2D1::RectF(left, top, left + shown.width * scale, top + shown.height * scale);
}

void ViewState::FitToWindow()
{
    Reset();
}

void ViewState::ActualSize()
{
    if (!HasImage() || IsActualSize())
        return;

    // Die betrachtete Stelle bleibt erhalten: wer in eine Ecke gezoomt hat und
    // auf Originalgröße wechselt, findet dort dieselbe Ecke wieder.
    const D2D1_POINT_2F center = Center();
    fit_ = false;
    scale_ = 1.0f;
    center_ = ClampCenter(center, 1.0f);
}

void ViewState::ZoomBy(float factor, D2D1_POINT_2F anchor)
{
    if (!HasImage() || factor <= 0.0f)
        return;

    const float previous = Scale();
    const float minScale = FitScale();
    const float next = std::clamp(previous * factor, minScale, kMaxScale);

    // Ganz herausgezoomt ist das Einpassen -- so landet wiederholtes
    // Verkleinern genau dort und der Knopf "Einpassen" wird wieder grau.
    if (next <= minScale * kEpsilon)
    {
        FitToWindow();
        return;
    }
    if (std::abs(next - previous) < 1e-6f)
        return;

    // Der Bildpunkt unter dem Anker bleibt liegen; er ist der Fixpunkt des Zooms.
    const D2D1_POINT_2F center = Center();
    const float dx = anchor.x - viewport_.width * 0.5f;
    const float dy = anchor.y - viewport_.height * 0.5f;
    const D2D1_POINT_2F fixed =
        D2D1::Point2F(center.x + dx / previous, center.y + dy / previous);

    fit_ = false;
    scale_ = next;
    center_ = ClampCenter(D2D1::Point2F(fixed.x - dx / next, fixed.y - dy / next), next);
}

void ViewState::ZoomBy(float factor)
{
    ZoomBy(factor, D2D1::Point2F(viewport_.width * 0.5f, viewport_.height * 0.5f));
}

void ViewState::PanBy(float dx, float dy)
{
    if (!CanPan())
        return;

    const float scale = Scale();
    const D2D1_POINT_2F center = Center();

    // Das Bild folgt dem Zeiger, der Ausschnitt wandert also entgegengesetzt.
    fit_ = false;
    scale_ = scale;
    center_ = ClampCenter(D2D1::Point2F(center.x - dx / scale, center.y - dy / scale), scale);
}

bool ViewState::IsActualSize() const
{
    return HasImage() && std::abs(Scale() - 1.0f) < 0.001f;
}

bool ViewState::CanZoomIn() const
{
    return HasImage() && Scale() < kMaxScale / kEpsilon;
}

bool ViewState::CanZoomOut() const
{
    return HasImage() && Scale() > FitScale() * kEpsilon;
}

bool ViewState::CanPan() const
{
    if (!HasImage())
        return false;

    const D2D1_SIZE_F shown = Shown();
    const float scale = Scale();
    return shown.width * scale > viewport_.width + 0.5f ||
           shown.height * scale > viewport_.height + 0.5f;
}
