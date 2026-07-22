# Prüfprogramme

Kleine, eigenständige Programme, die je eine Frage beantworten. Sie gehören
nicht zur Anwendung — sie sind der Beleg für Zahlen und Behauptungen, die in
[PLAN.md](../../PLAN.md) stehen. Wer ihnen nicht traut, lässt sie laufen:

```bat
tools\pruefungen\pruefen.ps1
tools\pruefungen\pruefen.ps1 -Nur brechen,gifdaten
```

Übersetzt wird nach `%TEMP%`; im Arbeitsbaum bleibt nichts liegen.

| Programm | Frage | belegt |
|---|---|---|
| [umgebung.cpp](umgebung.cpp) | Sind Direct2D und WIC da? | Abschnitt 1 |
| [decoder.cpp](decoder.cpp) | Welche Formate kann dieser Rechner? | Abschnitt 1 |
| [pixelformate.cpp](pixelformate.cpp) | Womit kommen die Bilder herein? | Abschnitt 3 |
| [gifdaten.cpp](gifdaten.cpp) | Was steht in einem GIF, und als welcher Typ? | Abschnitt 5 |
| [ablegen.cpp](ablegen.cpp) | Hält die Pfadentnahme aus einem HDROP? | Abschnitt 6 |
| [brechen.cpp](brechen.cpp) | Was nimmt WIC wirklich nicht mehr an? | Abschnitt 7 |

## Was sie heute sagen

**umgebung** — `D2D=0x00000000 WIC=0x00000000`, 14 Decoder. Beides ohne
Nachinstallieren vorhanden; das war die Voraussetzung der ganzen Wahl.

**decoder** — die Liste, aus der die Tabelle in Abschnitt 1 stammt. Bemerkens­wert
ist ihre Herkunft: die Anwendung fragt zur Laufzeit dieselbe Liste ab, statt
Endungen fest einzubauen. Ein nachinstalliertes Format erscheint dadurch von
selbst in der Ordnerliste.

**pixelformate** — `fax.tif` kommt mit **1 bpp BlackWhite** herein,
`mehrseitig.tif` mit 32bppBGRA. Ohne die Wandlung nach 32bppPBGRA bliebe das
Fax unsichtbar; deshalb läuft jeder Frame durch einen `IWICFormatConverter`.

**gifdaten** — die Datentypen, an denen Abschnitt 5 hängt: Verzögerung als
`VT_UI2` in Hundertstelsekunden, Aufräumregel als `VT_UI1`, Durchsichtigkeit
als `VT_BOOL`. Und der Unterschied, der beim Komponieren zählt: bei `anim.gif`
meldet `GetSize` für Bild 1 und 2 je 32×32, während `/imgdesc/Left` und `/Top`
sie auf 8,8 bzw. 20,20 setzen — wer nur `GetSize` liest, malt sie in die Ecke.

**ablegen** — alle fünf Fälle bestehen, darunter Umlaute mit Leerzeichen und
ein Pfad von 412 Zeichen. Geprüft wird die Größenrechnung: `DragQueryFileW`
meldet die Länge **ohne** Abschlusszeichen, will die Puffergröße aber **mit**.

**brechen** — die Pointe des ganzen Verzeichnisses: 64 verfälschte Bytes bei
20, 50 und 80 % der Datei, in PNG und JPEG, und **alle sechs kommen ohne
Fehler durch**. Das JPEG meldet „premature end of data segment" auf die
Konsole und liefert die Pixel trotzdem. Wer im Hintergrund-Thread auf
`WINCODEC_ERR_BADIMAGE` wartet, wartet lange — was den Fehlerweg wirklich
auslöst, ist ein Pfad, der verschwindet.
