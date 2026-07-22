#pragma once

#include "Common.h"

#include <wincodec.h>

#include <string>

class ImageDocument;

// Wie ein Druckversuch ausgegangen ist.
enum class PrintOutcome
{
    Printed,     // Auftrag liegt in der Warteschlange
    Cancelled,   // Dialog abgebrochen -- kein Fehler, also auch keine Meldung
    Failed,      // error traegt den Grund
};

// Was gedruckt werden soll.
struct PrintJob
{
    // Quelle der Seiten. Gedruckt wird aus der Datei, nicht aus der Anzeige:
    // fuer die Texturgrenze der Grafikkarte kann das gezeigte Bild verkleinert
    // worden sein, und die ID2D1Bitmap liesse sich ohnehin nicht zurueckholen.
    const ImageDocument* document = nullptr;

    // Fertig komponierte Leinwand eines animierten GIFs. Ist sie gesetzt, wird
    // genau sie gedruckt: die Rohframes eines GIFs sind Teilrechtecke und
    // ergaeben einzeln Fetzen statt Bilder.
    IWICBitmapSource* composed = nullptr;

    UINT currentPage = 0;       // 0-basiert, die gerade gezeigte Seite
    int rotationQuarters = 0;   // Drehung der Anzeige, Viertel im Uhrzeigersinn
};

// Zeigt den Druckdialog und druckt, was dort gewaehlt wurde. Laeuft im
// aufrufenden Thread; der Dialog ist modal, und was danach kommt, ist durch
// die Aufloesung des Geraets nach oben begrenzt.
PrintOutcome PrintImage(HWND owner, IWICImagingFactory* wic, const PrintJob& job,
                        std::wstring& error);

// Platz des Bildes auf dem Blatt, in Punkten des Geraets.
struct PrintPlacement
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// Eine Seite in einen Geraetekontext zeichnen -- zwischen StartPage und
// EndPage. Steht hier und nicht im Verborgenen, damit der Weg aufs Papier
// ohne Dialog nachzumessen ist; tools/pruefungen/drucken.cpp tut genau das.
// placement nimmt auf Wunsch das errechnete Rechteck auf und darf nullptr sein.
bool PrintPageToDC(HDC dc, IWICImagingFactory* wic, IWICBitmapSource* source,
                   int rotationQuarters, PrintPlacement* placement, std::wstring& error);
