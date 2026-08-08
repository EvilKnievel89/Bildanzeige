// Bildanzeige -- Copyright (C) 2026 EvilKnievel89
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Freie Software unter der GNU GPL, Version 3 oder einer späteren: weitergeben
// und verändern ist erlaubt, solange Quelltext und Lizenz mitgehen. Ohne jede
// Gewährleistung. Wortlaut in LICENSE, Erklärung in LIZENZ.md.

#pragma once

#include "Common.h"

#include <wincodec.h>

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

// Ergebnis eines Auftrags.
//
// Die Bitmap ist geräteunabhängig -- ein bloßer Pixelpuffer im Speicher. Die
// ID2D1Bitmap daraus entsteht erst im UI-Thread, denn Direct2D-Ressourcen
// gehören dem Thread, der sie erzeugt hat.
struct DecodeResult
{
    unsigned long long token = 0;
    UINT frameIndex = 0;
    ComPtr<IWICBitmap> bitmap;

    // Größe, wie sie in der Datei steht. Musste für die Texturgrenze
    // verkleinert werden, ist die Bitmap kleiner -- der Maßstab bezieht sich
    // aber weiter auf diese Zahlen, sonst zeigte "100 %" nicht die Originalgröße.
    UINT sourceWidth = 0;
    UINT sourceHeight = 0;

    std::wstring error;   // leer = geglückt
};

// Dekodierung auf einem eigenen Thread.
//
// Ein großes TIFF oder ein RAW braucht Sekunden; im UI-Thread stünde das
// Fenster derweil. Aufgetragen wird immer ein vollständiger Auftrag aus Pfad
// und Seitennummer -- der Worker öffnet die Datei selbst und gibt nur die
// fertige Bitmap heraus. Es wird also kein WIC-Objekt zwischen den Threads
// geteilt, sondern nur übergeben.
//
// Es wartet höchstens ein Auftrag: wer rasch durch einen Ordner blättert,
// will das letzte Bild sehen, nicht jedes dazwischen. Jeder Auftrag trägt eine
// Marke, an der überholte Ergebnisse erkannt und verworfen werden.
class DecodeWorker
{
public:
    ~DecodeWorker();

    bool Start(HWND notify, UINT message);
    void Stop();

    // Neuer Auftrag; ein noch wartender wird ersetzt. Liefert die Marke, unter
    // der das Ergebnis eintrifft, oder 0, wenn kein Thread läuft.
    unsigned long long Request(const std::wstring& path, UINT frameIndex, UINT maxTexture);

    // Holt das gemeldete Ergebnis ab. false, wenn keines (mehr) bereitliegt.
    bool Take(DecodeResult& out);

private:
    void Run();

    HWND notify_ = nullptr;
    UINT message_ = 0;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable signal_;

    bool quit_ = false;
    bool hasRequest_ = false;
    std::wstring path_;
    UINT frameIndex_ = 0;
    UINT maxTexture_ = 0;
    unsigned long long token_ = 0;

    bool hasResult_ = false;
    DecodeResult result_;
};
