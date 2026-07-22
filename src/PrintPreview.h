#pragma once

#include "Printer.h"

// Seitenansicht vor dem Drucken.
//
// Sie zeigt das Blatt so, wie es aus dem Gerät kommt -- und zwar nicht
// nachempfunden: gezeichnet wird mit derselben Funktion, die auch druckt
// (PrintPageToDC), nur in eine Metadatei statt in den Druckerkontext. Die
// Aufzeichnung wird dann auf den Schirm gespielt. Die Ansicht kann deshalb gar
// nicht von der Ausgabe abweichen; sie ist die Ausgabe, nur kleiner.
//
// Gerechnet wird mit dem Standarddrucker, denn zu diesem Zeitpunkt hat noch
// niemand einen gewählt. Sein Name steht im Fenstertitel: wer im Druckdialog
// danach ein anderes Gerät nimmt, bekommt dessen Blatt und nicht das gezeigte,
// und dann soll wenigstens dagestanden haben, worauf sich die Ansicht bezog.
//
// Liefert, was aus dem anschließenden Auftrag geworden ist, oder Cancelled,
// wenn die Ansicht ohne Druck geschlossen wurde.
PrintOutcome ShowPrintPreview(HWND owner, IWICImagingFactory* wic, const PrintJob& job,
                              std::wstring& error);
