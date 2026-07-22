# Werkzeuge

Zweierlei liegt hier: die **Erzeuger der Testdaten** und die
**[Prüfprogramme](pruefungen/)**. Jene legen an, womit geprüft wird; diese
beantworten die Fragen, auf denen [PLAN.md](../PLAN.md) steht — welche Decoder
es gibt, in welchem Format ein Fax hereinkommt, was WIC an einer beschädigten
Datei noch durchgehen lässt, was beim Drucken wirklich aufs Blatt kommt. Beide
sind mit einem Aufruf zu haben:

```bat
tools\mktestdaten.ps1              Testdaten neu erzeugen
tools\pruefungen\pruefen.ps1       alle Prüfungen laufen lassen
```

## Erzeuger der Testdaten

Alles in [testdata/](../testdata/) entsteht hier. Kein Bild ist irgendwo
heruntergeladen oder von Hand gemalt; jede Datei hat einen Zweck, und der steht
im Kopf des jeweiligen Erzeugers.

```bat
tools\mktestdaten.ps1
```

Das genügt: übersetzt die vier C++-Erzeuger nach `%TEMP%`, ruft sie samt der
beiden PowerShell-Skripte auf und räumt hinterher auf. Mit `-Ziel <Ordner>`
geht es woandershin, etwa zum Vergleich mit dem Bestand. MSVC muss nicht schon
eingerichtet sein — das Skript sucht die Installation selbst (`vswhere`).

| Erzeuger | Dateien |
|---|---|
| [mkgrund.ps1](mkgrund.ps1) | `gross.png`, `klein.png`, `ordner/notiz.txt` |
| [mktiff.ps1](mktiff.ps1) | `mehrseitig.tif`, `fax.tif` |
| [mkgif.cpp](mkgif.cpp) | `anim.gif`, `spur.gif`, `disposal.gif`, `zweimal.gif` |
| [mkordner.cpp](mkordner.cpp) | `ordner/Bild1.png`, `Bild2.png`, `Bild10.png`, `dreh1..8.jpg` |
| [mkgrenzen.cpp](mkgrenzen.cpp) | `riesig.jpg`, `ueberbreit.png`, `kaputt.png` |
| [mklangsam.cpp](mklangsam.cpp) | `langsam.png` |

Die TIFFs entstehen über GDI+, alles andere über WIC — also über denselben
Weg, den die Anwendung zum Lesen nimmt. Jeder C++-Erzeuger liest hinterher
zurück, was er geschrieben hat, und gibt Maße, Seitenzahl, Verzögerungen und
EXIF-Orientierung aus. Eine Datei, die schon beim Zurücklesen nicht stimmt,
taugt als Prüfstück nichts.

## Zwei Erzeuger legen mehr an, als in testdata gehört

`mkordner.cpp` schreibt alle acht EXIF-Orientierungen; gebraucht werden
`dreh1` als Vergleichsstück und `dreh6` als der Fall, der hochkant erscheinen
muss. `mkgif.cpp` schreibt zusätzlich `gross_a.gif` und `gross_b.gif`, zwei
13,6-MB-GIFs beidseits der Zwischenspeichergrenze (PLAN.md, Abschnitt 5) — die
gehören zu einer Speichermessung, nicht zur Anzeigeprüfung, und wären 40 MB im
Arbeitsbaum. `mktestdaten.ps1` entfernt beides wieder und sagt, was es entfernt
hat. Wer es braucht, ruft den Erzeuger einzeln auf.

## Wie genau ist „reproduzierbar"?

Neu erzeugt und Byte für Byte gegen den Bestand gehalten: **15 der 18 Dateien
sind identisch**. Die Encoder von WIC und GDI+ arbeiten also deterministisch,
und die Erzeuger sind wirklich die Herkunft dieser Dateien, nicht bloß etwas
Ähnliches.

Die drei übrigen haben einen Grund:

- `gross.png` und `klein.png` — deren ursprüngliches Skript ist nicht erhalten.
  `mkgrund.ps1` ist ein Nachbau nach dem Augenschein: gleiche Maße, gleiche
  Farben (`DarkSlateBlue` → `Orange`, `Crimson`), gleiche Aufschrift. Die
  Dateien im Arbeitsbaum stammen inzwischen aus dem Nachbau, damit Werkzeug und
  Bestand zusammenpassen.
- `anim.gif` — war älter als `mkgif.cpp` und fehlte darin. Jetzt entsteht es
  dort mit: 64×64, drei Einzelbilder, Verzögerungen 500/250/1000 ms,
  Aufräumregel 2 — Struktur für Struktur dieselbe wie zuvor, nur anders
  gepackt (331 statt 261 Byte).
