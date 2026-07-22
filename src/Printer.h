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

// Beim Drucken gilt diese Feinheit als Obergrenze fuer das Zwischenbild.
// Darueber ist am Blatt kein Unterschied mehr zu sehen, der Speicher waechst
// aber weiter -- Naeheres in PLAN.md, Abschnitt 9.
constexpr int kPrintDpi = 600;

// Eine Seite in einen Geraetekontext zeichnen -- beim Drucken zwischen
// StartPage und EndPage. Steht hier und nicht im Verborgenen, weil zwei andere
// denselben Weg brauchen: die Seitenansicht zeichnet damit in eine Metadatei,
// und tools/pruefungen/drucken.cpp misst damit nach, was aufs Blatt kommt.
//
// maxDpi begrenzt die Feinheit des Zwischenbildes. Die Seitenansicht setzt hier
// die Feinheit des Bildschirms ein: in Druckauflaesung zu rechnen kostete
// Sekunden und zeigte am Schirm keinen Punkt mehr.
//
// placement nimmt auf Wunsch das errechnete Rechteck auf und darf nullptr sein.
bool PrintPageToDC(HDC dc, IWICImagingFactory* wic, IWICBitmapSource* source,
                   int rotationQuarters, int maxDpi, PrintPlacement* placement,
                   std::wstring& error);

// Rahmen fuer eine Metadatei, die eine mit PrintPageToDC gezeichnete Seite
// aufnehmen soll: der bedruckbare Bereich des Geraets, in Hundertstelmillimetern.
//
// Das ist keine Feinheit, sondern der Unterschied zwischen richtig und falsch.
// CreateEnhMetaFile setzt den Rahmen ohne diese Angabe auf das kleinste
// Rechteck um das Gezeichnete -- beim Abspielen fuellte das Bild dann die ganze
// Zielflaeche, gleichgueltig, wo auf dem Blatt es hingehoert. Die Einpassung
// waere unsichtbar und die Ansicht eine Luege.
RECT PrintMetafileFrame(HDC printerDC);
