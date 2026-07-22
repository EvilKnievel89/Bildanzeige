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
    Failed,      // error trägt den Grund
};

// Was gedruckt werden soll.
struct PrintJob
{
    // Quelle der Seiten. Gedruckt wird aus der Datei, nicht aus der Anzeige:
    // für die Texturgrenze der Grafikkarte kann das gezeigte Bild verkleinert
    // worden sein, und die ID2D1Bitmap ließe sich ohnehin nicht zurückholen.
    const ImageDocument* document = nullptr;

    // Fertig komponierte Leinwand eines animierten GIFs. Ist sie gesetzt, wird
    // genau sie gedruckt: die Rohframes eines GIFs sind Teilrechtecke und
    // ergäben einzeln Fetzen statt Bilder.
    IWICBitmapSource* composed = nullptr;

    UINT currentPage = 0;       // 0-basiert, die gerade gezeigte Seite
    int rotationQuarters = 0;   // Drehung der Anzeige, Viertel im Uhrzeigersinn

    // Kleine Bilder aufs Blatt vergrößern. Standardmäßig aus: eingepasst wird
    // nur nach unten. In der Seitenansicht steht dafür ein Kästchen.
    bool enlargeToFit = false;
};

// Zeigt den Druckdialog und druckt, was dort gewählt wurde. Läuft im
// aufrufenden Thread; der Dialog ist modal, und was danach kommt, ist durch
// die Auflösung des Geräts nach oben begrenzt.
PrintOutcome PrintImage(HWND owner, IWICImagingFactory* wic, const PrintJob& job,
                        std::wstring& error);

// Platz des Bildes auf dem Blatt, in Punkten des Geräts.
struct PrintPlacement
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// Beim Drucken gilt diese Feinheit als Obergrenze für das Zwischenbild.
// Darüber ist am Blatt kein Unterschied mehr zu sehen, der Speicher wächst
// aber weiter -- Näheres in PLAN.md, Abschnitt 9.
constexpr int kPrintDpi = 600;

// Eine Seite in einen Gerätekontext zeichnen -- beim Drucken zwischen
// StartPage und EndPage. Steht hier und nicht im Verborgenen, weil zwei andere
// denselben Weg brauchen: die Seitenansicht zeichnet damit in eine Metadatei,
// und tools/pruefungen/drucken.cpp misst damit nach, was aufs Blatt kommt.
//
// enlargeToFit entscheidet über kleine Bilder: ohne die Angabe wird höchstens
// auf die eigene Größe des Bildes gebracht -- seine Punktzahl, umgerechnet über
// die Feinheit, die in der Datei steht (Näheres in PLAN.md, Abschnitt 9).
//
// maxDpi begrenzt die Feinheit des Zwischenbildes. Die Seitenansicht setzt hier
// die Feinheit des Bildschirms ein: in Druckauflösung zu rechnen kostete
// Sekunden und zeigte am Schirm keinen Punkt mehr.
//
// placement nimmt auf Wunsch das errechnete Rechteck auf und darf nullptr sein.
bool PrintPageToDC(HDC dc, IWICImagingFactory* wic, IWICBitmapSource* source,
                   int rotationQuarters, bool enlargeToFit, int maxDpi,
                   PrintPlacement* placement, std::wstring& error);

// Rahmen für eine Metadatei, die eine mit PrintPageToDC gezeichnete Seite
// aufnehmen soll: der bedruckbare Bereich des Geräts, in Hundertstelmillimetern.
//
// Das ist keine Feinheit, sondern der Unterschied zwischen richtig und falsch.
// CreateEnhMetaFile setzt den Rahmen ohne diese Angabe auf das kleinste
// Rechteck um das Gezeichnete -- beim Abspielen füllte das Bild dann die ganze
// Zielfläche, gleichgültig, wo auf dem Blatt es hingehört. Die Einpassung
// wäre unsichtbar und die Ansicht eine Lüge.
RECT PrintMetafileFrame(HDC printerDC);
