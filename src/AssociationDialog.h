#pragma once

#include "Common.h"

// Fenster für die Dateizuordnungen, erreichbar über das Fenstermenü.
//
// Es zeigt die Endungen, die die Anwendung öffnen kann, als Ankreuzliste: was
// angehakt ist, steht anschließend in der Registrierung, was nicht, wird daraus
// entfernt. In welchen Zweig geschrieben wird, sagt der Kopf des Fensters --
// gewählt wird er nicht, er hängt an den Rechten des Laufs (FileAssociation.h).
//
// Modal wie die Seitenansicht: solange es steht, ist das Hauptfenster gesperrt.
void ShowAssociationDialog(HWND owner);
