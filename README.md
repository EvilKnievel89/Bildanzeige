# Bildanzeige

Schlanker Bildbetrachter für Windows im Geist der alten *Windows Bild- und
Faxanzeige*. Win32 + WIC + Direct2D, keine externen Abhängigkeiten.

Planung und Entwurf: [PLAN.md](PLAN.md)

## Bauen

Voraussetzungen: Visual Studio 2022 (MSVC v143), Windows SDK, CMake ≥ 3.21, Ninja.

Der Ninja-Generator braucht die MSVC-Umgebung, daher zuerst `vcvars64.bat`:

```bat
"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake --preset msvc-x64
cmake --build --preset release
```

Ergebnis: `build\Release\Bildanzeige.exe` — eine einzelne, portable EXE von
437 KB. Sie braucht kein Redistributable: die CRT ist statisch eingebunden, und
importiert werden nur Systembibliotheken (`d2d1`, `ole32`, `shlwapi`,
`shell32`, `user32`, `comctl32`, `kernel32`). Anwendungssymbol, Manifest und
Versionsangaben stecken darin; kopieren genügt, installiert wird nichts.

Die Versionsnummer steht allein in der `project()`-Zeile der `CMakeLists.txt`.
Ressourcendatei und Manifest bekommen sie beim Bauen zugereicht — wer sie
ändert, ändert sie überall.

Für einen Debug-Build `--preset debug` statt `release`.

## Ausführen

```bat
build\Release\Bildanzeige.exe testdata\gross.png
```

Statt einer Datei lässt sich auch ein Ordner übergeben; dann erscheint das erste
Bild darin. Ohne Argument öffnet sich ein leeres Fenster. Dateien und Ordner
können auch auf das Fenster gezogen werden.

| Taste | Funktion |
|---|---|
| `←` / `→` | vorheriges / nächstes Bild im Ordner |
| `Leertaste` | Animation anhalten / fortsetzen |
| `Bild↓` / `Bild↑` | nächste / vorherige Seite bzw. Einzelbild |
| `Pos1` / `Ende` | erste / letzte Seite |
| `+` / `-`, Mausrad | vergrößern / verkleinern |
| `Strg`+`B` | einpassen (beste Anpassung) |
| `Strg`+`0` | Originalgröße |
| `Strg`+Pfeiltaste | Ausschnitt verschieben |
| `Strg`+`L` / `Strg`+`R` | links / rechts drehen |
| `F11`, Doppelklick aufs Bild | Vollbild ein / aus |
| `Esc` | Vollbild verlassen, sonst schließen |

Dieselben Funktionen liegen auf der Icon-Leiste am unteren Rand. Die
Schrittknöpfe werden nur eingeblendet, wenn die Datei mehrere Seiten hat.
Rotation wirkt ausschließlich in der Anzeige — Dateien bleiben unverändert.

Der Bildwechsel geht durch alle anzeigbaren Dateien desselben Ordners, sortiert
wie im Explorer (`Bild2` vor `Bild10`). Am Anfang und am Ende wird der jeweilige
Knopf grau — dort ist Schluss, es wird nicht umgelaufen. Welche Endungen dazu
zählen, richtet sich nach den auf dem Rechner vorhandenen Decodern; ein
nachinstalliertes Format erscheint dadurch von selbst mit in der Reihe. Die
Ordnerliste entsteht beim Öffnen: eine inzwischen gelöschte Datei wird beim
Weiterblättern stillschweigend übergangen.

Fotos mit EXIF-Vermerk zur Orientierung werden von sich aus richtig herum
gezeigt. Diese Lage ist der Ausgangspunkt, auf den ein späteres Drehen von Hand
aufsetzt. Gespiegelte Vermerke werden nur in ihrem Drehanteil berücksichtigt.

Animierte GIFs werden abgespielt, nicht durchgeblättert. Dafür erscheint
zwischen den Schrittknöpfen ein Wiedergabeknopf; im Halt treten die
Schrittknöpfe durch die Einzelbilder, und hinter dem letzten geht es wieder vorn
weiter. Zoom, Ausschnitt und Drehung bleiben über den Bildwechsel hinweg
stehen — wer in ein Detail hineingezoomt hat, findet es im nächsten Einzelbild
wieder. Die Bildzählung steht nur im Halt im Fenstertitel; während der
Wiedergabe ließe sie ihn nur flimmern.

Ist das Bild größer als das Fenster, lässt es sich mit gedrückter linker
Maustaste ziehen. Das Mausrad zoomt auf die Stelle unter dem Zeiger. Ein grauer
Knopf sagt zugleich, wie das Bild gerade steht: „Einpassen" ist grau, solange
eingepasst ist, „Originalgröße" bei 100 %. Der Maßstab steht im Fenstertitel.

Herauszoomen endet beim Einpassen — kleiner als nötig wird nicht gezeigt, und
kleine Bilder werden nicht aufgeblasen. Oberhalb von 100 % wird nicht geglättet,
damit bei einem Scan die vorhandenen Pixel sichtbar bleiben.

Im Vollbild bleibt die Icon-Leiste stehen. Ohne Rahmen und Menü ist sie der
einzige sichtbare Rückweg, und vierzig Punkte am unteren Rand kosten auf einem
großen Schirm knapp drei Prozent der Fläche.

Dekodiert wird nebenher, das Fenster bleibt also auch bei einem großen Scan
sofort bedienbar. Braucht ein Bild länger als eine Fünftelsekunde, bleibt
zunächst das vorige stehen; erst danach wird die Fläche geleert und der Titel
sagt „wird geladen …". Bei einem gewöhnlichen Foto tritt das nie in
Erscheinung. Wer rasch durchblättert, überholt dabei: nur das zuletzt
angesteuerte Bild wird auch gezeigt.

Bilder, die größer sind als die Grafikkarte an Textur annimmt (je nach Gerät
8000 bis 16000 Punkte Kantenlänge), werden verkleinert dargestellt. Der Maßstab
im Titel bezieht sich trotzdem auf die Maße der Datei — „Originalgröße"
bedeutet also weiterhin, was in der Datei steht.

## Stand

**Version 1.0.0**, alle acht Meilensteine abgeschlossen — Fenster,
Direct2D-Darstellung, Datei per Kommandozeile, Multi-Frame-Dekodierung,
Seitenwechsel, Icon-Leiste, Rotation, Zoom, Verschieben, Einpassen und
Originalgröße, GIF-Animation, Ordnernavigation, Drag & Drop,
EXIF-Orientierung, Hintergrund-Dekodierung, Vollbild, DPI-Wechsel,
Fehlerbehandlung sowie Anwendungssymbol, Versionsangaben und Release-Build.

Was bewusst nicht in v1 steckt — Rotation speichern, Löschen, Drucken,
Diashow, Registrierung als Standardanwendung — steht in [PLAN.md](PLAN.md).

## Testdaten

`testdata/` enthält Beispieldateien zum Prüfen der Darstellung. Sie entstehen
sämtlich aus [tools/](tools/) — `tools\mktestdaten.ps1` legt sie neu an:

| Datei | Zweck |
|---|---|
| `gross.png` | 3000×2000 — muss verkleinert eingepasst werden |
| `klein.png` | 120×80 — darf **nicht** hochskaliert werden |
| `mehrseitig.tif` | 3 Seiten à 900×600, 400×900, 1400×500 — Fit muss pro Seite rechnen |
| `fax.tif` | 2 Seiten, 1 bpp CCITT G4 — ohne Formatkonvertierung unsichtbar |
| `anim.gif` | 3 Frames, Teilrechtecke, Delays 50/25/100 |
| `spur.gif` | 5 Frames, 240×160 — nach Bild N müssen genau N Quadrate stehen; der durchsichtige Rand jedes Teilrechtecks darf den Hintergrund nicht überschreiben |
| `disposal.gif` | 4 Frames — Aufräumregeln 1, 2 und 3 nacheinander: Rot muss verschwinden und ein Loch hinterlassen, Grün muss verschwinden und das Loch bestehen bleiben |
| `zweimal.gif` | 3 Frames, Wiederholungszähler 2 — muss nach zwei Durchläufen auf dem letzten Einzelbild stehenbleiben |
| `dreh1.jpg` | 1200×300, EXIF-Orientierung 1 — Vergleichsstück, weißer Block oben links |
| `dreh6.jpg` | dieselben Pixel, EXIF-Orientierung 6 — muss hochkant erscheinen, Block oben **rechts** |
| `ordner/` | `Bild1`, `Bild2`, `Bild10` mit 1, 2 bzw. 10 Quadraten — die Reihenfolge muss 1, 2, 10 sein, nicht 1, 10, 2; `notiz.txt` darf nicht in der Reihe auftauchen |
| `langsam.png` | 60000×2000 — 120 Megapixel Eingang, aber nur 34 MB Textur: dekodiert lange und braucht wenig Speicher. Das Fenster muss dabei ansprechbar bleiben und der Ladezustand erscheinen |
| `riesig.jpg` | 8000×8000 — der umgekehrte Fall: 256 MB Textur. Zeigt, was vom Stillstand zwangsläufig übrigbleibt |
| `ueberbreit.png` | 20000×400 — jenseits der Texturgrenze. Der Maßstab im Titel muss sich auf 20000 beziehen (4 %), nicht auf die verkleinerte Textur (5 %) |
| `kaputt.png` | auf ein Fünftel abgeschnitten — WIC zeigt es trotzdem an; die Datei belegt, dass eine Beschädigung hier keinen Fehler ergibt |
