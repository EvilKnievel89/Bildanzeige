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
| [decoder.cpp](decoder.cpp) | Welche Formate kann dieser Rechner wirklich? | Abschnitt 1, 7 |
| [pixelformate.cpp](pixelformate.cpp) | Womit kommen die Bilder herein? | Abschnitt 3 |
| [gifdaten.cpp](gifdaten.cpp) | Was steht in einem GIF, und als welcher Typ? | Abschnitt 5 |
| [ablegen.cpp](ablegen.cpp) | Hält die Pfadentnahme aus einem HDROP? | Abschnitt 6 |
| [brechen.cpp](brechen.cpp) | Was nimmt WIC wirklich nicht mehr an? | Abschnitt 7 |
| [drucken.cpp](drucken.cpp) | Was kommt beim Drucken heraus? | Abschnitt 9 |

## Was sie heute sagen

**umgebung** — `D2D=0x00000000 WIC=0x00000000`, 14 Decoder. Beides ohne
Nachinstallieren vorhanden; das war die Voraussetzung der ganzen Wahl.

**decoder** — die Liste, aus der die Tabelle in Abschnitt 1 stammt. Bemerkens­wert
ist ihre Herkunft: die Anwendung fragt zur Laufzeit dieselbe Liste ab, statt
Endungen fest einzubauen. Ein nachinstalliertes Format erscheint dadurch von
selbst in der Ordnerliste.

Seit dem WebP-Befund erzeugt sie jeden Decoder außerdem einmal probeweise, denn
angemeldet heißt nicht vorhanden. Auf dieser Maschine sind **14 angemeldet und
13 brauchbar**: JPEG XL scheitert mit `0x88982F8B`, weil die zugehörige
Erweiterung aus dem Microsoft Store fehlt. Das ist derselbe Fall, den ein
Windows Server ohne Store bei WebP vor sich hat — die Prüfung beantwortet also
in einer Zeile, ob ein Rechner betroffen ist und welches Format es trifft. Ein
nicht erzeugbarer Decoder ist deshalb ein Befund über den Rechner, keine
Beanstandung; der Rückgabewert bleibt 0.

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

**drucken** — die einzige Prüfung, die Quellen der Anwendung mitübersetzt
(`Printer.cpp`, `ImageDocument.cpp`, `Common.cpp`); dieselben Quellen tragen
auch die Seitenansicht. Sie rechnet mit genau der
Funktion, die auch druckt: einen Weg, den man ohne Drucker nicht nachrechnen
kann, mit einer nachgebauten Fassung zu prüfen wäre wertlos. Drei Befunde. Der
bedruckbare Bereich ist unsymmetrisch — beim Kyocera 99 Punkte oben und 103
unten —, weshalb auf dem *Blatt* zentriert wird und nicht darin. Alle Seiten
von `gross.png` und `mehrseitig.tif`, letztere um 90 Grad gedreht, kommen
unverzerrt, eingepasst und mittig heraus. Und der Fall, den man im PDF nicht
sieht: eine zur Hälfte durchsichtige Vorlage ergibt **84 600 rote, 84 600 weiße,
null schwarze und 310 800 unbemalte Punkte** — ohne das Unterlegen mit Weiß käme
dort Schwarz heraus.

Der unbemalte Rest ist erst nachträglich zur Bedingung geworden, und das ist die
lehrreiche Stelle: vorher stand dort **0**, weil die Metadatei ohne Rahmen
angelegt wurde und das Bild beim Abspielen die ganze Fläche füllte. Alle Haken
waren trotzdem grün — die Prüfung auf Weiß statt Schwarz stimmte ja. Dass bei
einer 200 × 100 großen Vorlage auf A4 nichts unbemalt bleiben soll, hätte
auffallen müssen. Seither besteht sie darauf.

Sie legt PDFs an und braucht deshalb ein Arbeitsverzeichnis; `pruefen.ps1`
arbeitet ohnehin in `%TEMP%`, es bleibt also nichts liegen. Mit
`-drucker "<Name>"` geht der Auftrag an ein anderes Gerät als „Microsoft Print
to PDF".
