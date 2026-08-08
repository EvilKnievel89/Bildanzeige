// Bildanzeige -- Copyright (C) 2026 EvilKnievel89
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Freie Software unter der GNU GPL, Version 3 oder einer späteren: weitergeben
// und verändern ist erlaubt, solange Quelltext und Lizenz mitgehen. Ohne jede
// Gewährleistung. Wortlaut in LICENSE, Erklärung in LIZENZ.md.

#pragma once

#include <d2d1.h>

// Abbildung des Bildes in die Bildfläche: Drehung, Maßstab, Ausschnitt.
//
// Der Ausschnitt wird als der Bildpunkt geführt, der in der Mitte der
// Bildfläche liegt -- nicht als Verschiebung in Fensterpixeln. Dadurch bleibt
// beim Zoomen und beim Größenändern des Fensters dieselbe Stelle im Blick,
// statt dass das Bild unter dem Zeiger fortwandert.
//
// Es gibt genau zwei Zustände: eingepasst (der Maßstab folgt dem Fenster) und
// frei (Maßstab und Ausschnitt sind gesetzt). Herauszoomen endet stets wieder
// beim Einpassen; kleiner als nötig wird nicht gezeigt.
class ViewState
{
public:
    void SetImageSize(D2D1_SIZE_F size);
    void SetViewport(D2D1_SIZE_F size);
    void Reset();                       // zurück aufs Einpassen, Drehung bleibt

    void Rotate(int quarters);
    void SetRotation(int quarters);     // Startdrehung, etwa aus EXIF
    int Rotation() const { return rotation_; }

    void FitToWindow();
    void ActualSize();
    void ZoomBy(float factor, D2D1_POINT_2F anchor);   // Anker in Fensterpixeln
    void ZoomBy(float factor);                          // um die Mitte
    void PanBy(float dx, float dy);                     // in Fensterpixeln

    D2D1_SIZE_F ImageSize() const { return image_; }

    // Die Größe, in der das Bild dasteht: bei einer Vierteldrehung sind Breite
    // und Höhe vertauscht. Danach richtet sich das Fenster.
    D2D1_SIZE_F ShownSize() const { return Shown(); }

    bool HasImage() const;
    bool IsFit() const { return fit_; }
    bool IsActualSize() const;
    bool CanZoomIn() const;
    bool CanZoomOut() const;
    bool CanPan() const;

    float Scale() const;
    D2D1_RECT_F DestRect() const;   // Zielrechteck der gedrehten Ansicht

private:
    D2D1_SIZE_F Shown() const;      // Größe nach Drehung
    float FitScale() const;
    D2D1_POINT_2F Center() const;
    D2D1_POINT_2F ClampCenter(D2D1_POINT_2F center, float scale) const;

    D2D1_SIZE_F image_{};
    D2D1_SIZE_F viewport_{};
    int rotation_ = 0;      // Vielfache von 90 Grad, 0..3
    bool fit_ = true;
    float scale_ = 1.0f;
    D2D1_POINT_2F center_{};
};
