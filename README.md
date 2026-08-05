# Bildanzeige

Schlanker Bildbetrachter für Windows im Geist der alten *Windows Bild- und
Faxanzeige*. Win32 + WIC + Direct2D, keine externen Abhängigkeiten.

![Screenshot of ui.](/screenshots/screenshot-ui.png)

Planung und Entwurf: [PLAN.md](PLAN.md) · Lizenz: [MIT mit Commons
Clause](LICENSE) — alles erlaubt außer Verkaufen

## Bauen

Voraussetzungen: Visual Studio 2022 (MSVC v143), Windows SDK, CMake ≥ 3.21, Ninja.

Der Ninja-Generator braucht die MSVC-Umgebung, daher zuerst `vcvars64.bat`:

```bat
"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake --preset msvc-x64
cmake --build --preset release
```

Ergebnis: `build\Release\Bildanzeige.exe` — eine einzelne, portable EXE von
552 KB. Sie braucht kein Redistributable: die CRT ist statisch eingebunden, und
importiert werden nur Systembibliotheken (`d2d1`, `ole32`, `shlwapi`,
`shell32`, `user32`, `comctl32`, `comdlg32`, `gdi32`, `winspool.drv`,
`advapi32`, `kernel32`).
Anwendungssymbol, Manifest und Versionsangaben stecken darin; kopieren genügt,
installiert wird nichts.

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

Zwei Schalter richten die Dateizuordnungen ohne Fenster ein — siehe unten:

```bat
Bildanzeige.exe /registrieren
Bildanzeige.exe /abmelden
```

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
| `Strg`+`P` | Seitenansicht (von dort drucken) |
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

**WebP, HEIC, AVIF und JPEG XL sind ein Sonderfall.** Windows meldet diese
Decoder zwar selbst an, liefert sie aber nicht mit — sie stecken in
Erweiterungen aus dem Microsoft Store. Fehlt eine, lässt sich die Datei nicht
öffnen, obwohl das Format in der Liste steht. Auf Windows Server gibt es keinen
Store, dort fehlen sie im Auslieferungszustand alle. Die Meldung sagt in diesem
Fall, welche Erweiterung gemeint ist und unter welcher Kennung sie im Store
steht; nachinstalliert wird sie sofort mitbenutzt. Welche Decoder auf einem
Rechner nicht nur eingetragen, sondern auch brauchbar sind, beantwortet
`tools\pruefungen\pruefen.ps1 -Nur decoder`.

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

Das **Fenster nimmt die Größe des Bildes an** — ein Bildpunkt ist ein
Bildschirmpunkt, und unter der Bildfläche kommt die Icon-Leiste hinzu. Es
wächst dabei nicht über den Arbeitsbereich hinaus: was größer ist als der
Bildschirm, endet an dessen Rand und wird wie gewohnt in die Fläche eingepasst.
Nach unten begrenzen es die Knöpfe der Leiste, damit sie bei einem
Vorschaubildchen nicht abgeschnitten stehen. Eine Vierteldrehung vertauscht
Breite und Höhe, das Fenster geht mit. Im Vollbild und im maximierten Zustand
bleibt es, wie es ist: dort ist die Größe ausdrücklich gesetzt.

Ist das Bild größer als das Fenster, lässt es sich mit gedrückter linker
Maustaste ziehen. Das Mausrad zoomt auf die Stelle unter dem Zeiger. Ein grauer
Knopf sagt zugleich, wie das Bild gerade steht: „Einpassen" ist grau, solange
eingepasst ist, „Originalgröße" bei 100 %. Der Maßstab steht im Fenstertitel.

Herauszoomen endet beim Einpassen — kleiner als nötig wird nicht gezeigt, und
kleine Bilder werden nicht aufgeblasen. Oberhalb von 100 % wird nicht geglättet,
damit bei einem Scan die vorhandenen Pixel sichtbar bleiben.

Der Druckknopf öffnet zuerst die **Seitenansicht**: das Blatt so, wie es aus
dem Gerät kommt, samt dem Rand, den der Drucker nicht erreicht. Sie ist keine
Nachempfindung — gezeichnet wird mit derselben Funktion, die auch druckt, nur
in eine Metadatei statt in den Drucker. Bei mehrseitigen Dateien wird darin mit
`Bild↑`/`Bild↓` oder den Pfeilknöpfen geblättert; von dort führt „Drucken …"
in den Druckdialog. Gerechnet wird mit dem Standarddrucker, und der steht im
Fenstertitel.

Gedruckt wird die gezeigte Seite, in der Drehung der Anzeige, auf dem Blatt
zentriert. Eingepasst wird dabei nur nach unten: was größer ist als das Blatt,
wird verkleinert, was kleiner ist, behält seine eigene Größe. Die ergibt sich
aus der Feinheit, die in der Datei steht — 120 × 80 Punkte mit 96 dpi sind gut
drei mal zwei Zentimeter, gleichgültig, wie fein der Drucker ist. Wer kleine
Bilder doch aufs ganze Blatt bringen will, hakt in der Seitenansicht „Kleine
Bilder vergrößern" an. Zoom und Ausschnitt bleiben außen vor — sie sind die
Lupe, mit der man das Bild betrachtet, nicht das Bild. Eine Animation zählt als
eine Seite. Während des Druckens steht das Fenster.

Windows 11 hat den Druckdialog gegen einen eigenen ausgetauscht. Dessen
Vorschaufeld bleibt bei jeder Win32-Anwendung leer („Diese App unterstützt
keine Seitenansicht"), und „Aktuelle Seite" bietet er nicht mehr an — beides
liegt an ihm, nicht an dieser Anwendung. Die eigene Seitenansicht schließt die
erste Lücke; für die zweite blättert man in ihr zur gewünschten Seite, die dann
als aktuelle Seite in den Auftrag geht. Näheres in [PLAN.md](PLAN.md),
Abschnitt 9.

Ganz rechts in der Leiste, abgesetzt von den übrigen Knöpfen, steht ein
**Zahnrad**. Es öffnet ein kleines Fenster mit einer einzigen Einstellung:
*Klassischen Windows-Druckdialog verwenden*. Windows 11 hat den Druckdialog
aller Win32-Anwendungen gegen einen eigenen ausgetauscht; der Haken holt den
alten zurück, indem er unter
`Software\Microsoft\Print\UnifiedPrintDialog` den Wert
`PreferLegacyPrintDialog` setzt. Der Stand kommt bei jedem Öffnen frisch aus
der Registrierung, geschrieben wird erst mit `OK`.

Die Umstellung wirkt beim nächsten Druckdialog — `comdlg32.dll` liest den Wert
bei jedem Aufruf neu, und sie läuft im Prozess der druckenden Anwendung. Ein
Neustart von Windows, des Explorers oder der Anwendung ist dafür nicht nötig.
Die Einstellung gilt für alle Anwendungen, nicht nur für die Bildanzeige.
Geschrieben wird nach `HKEY_CURRENT_USER`; nur wenn die Bildanzeige erhöht
läuft **und** unter `HKEY_LOCAL_MACHINE` bereits ein solcher Wert steht, geht
sie dorthin. Angelegt wird er dort nicht — ein rechnerweiter Eintrag, den
vorher niemand gesetzt hat, entschiede für alle Benutzer mit.

Im **Fenstermenü** (Alt+Leertaste oder Rechtsklick auf die Titelleiste) steht
vor „Schließen" der Eintrag „Dateizuordnungen …". Dort lässt sich die
Bildanzeige für die gängigen Bildformate eintragen — angehakt wird jede Endung
einzeln, `Übernehmen` schreibt genau die angehakten und nimmt die übrigen
zurück. Ein Haken weniger ist damit zugleich das Abmelden; sind am Ende alle
fort, bleibt nichts stehen.

Wohin die Einträge gehen, entscheidet der Start: gewöhnlich gestartet nach
`HKEY_CURRENT_USER` (nur für den angemeldeten Benutzer), als Administrator
gestartet nach `HKEY_LOCAL_MACHINE` (für alle Benutzer des Rechners). Der Kopf
des Fensters sagt, welcher der beiden Fälle gerade zutrifft. Dieselbe Regel
gilt für `/registrieren` und `/abmelden`, die den gängigen Satz Endungen ohne
Rückfrage eintragen bzw. sämtliche Einträge zurücknehmen; ihre Auskunft ist der
Rückgabewert (0 geglückt, 1 nicht), damit sie sich aus einem Skript heraus
aufrufen lassen.

**Eingetragen ist nicht dasselbe wie Standard.** Der Eintrag stellt die
Bildanzeige unter „Öffnen mit" und in den Windows-Einstellungen zur Wahl; wer
sie zum Standard machen will, trifft diese Wahl dort. Seit Windows 8 kann das
keine Anwendung mehr für den Benutzer erledigen — der Knopf „Als Standard
festlegen …" führt deshalb auf direktem Weg zur richtigen Seite der
Einstellungen. Wer die Bildanzeige später abmeldet und für eine Endung ihr
Standard war, wird von Windows beim nächsten Doppelklick wieder gefragt.

Zur Wahl stehen sechzehn Endungen: die elf, die jede Windows-Installation von
sich aus dekodiert (`.bmp`, `.dib`, `.gif`, `.ico`, `.jfif`, `.jpe`, `.jpeg`,
`.jpg`, `.png`, `.tif`, `.tiff`), und die modernen Formate `.avif`, `.heic`,
`.heif`, `.jxl` und `.webp`. Deren Decoder kommen aus Erweiterungen des
Microsoft Store, und angemeldet heißt bei ihnen nicht vorhanden — die
Bildanzeige erzeugt sie deshalb beim Öffnen des Fensters einmal probeweise.
Was sich erzeugen lässt, ist vorgehakt; hinter dem Rest steht „(Erweiterung
fehlt)", und er bleibt ungehakt, damit die Anwendung sich kein Format an sich
zieht, das sie hier nur mit einer Fehlermeldung beantworten könnte. Ankreuzen
lässt er sich trotzdem, und nach dem Nachinstallieren genügt ein erneutes
`Übernehmen` bzw. `/registrieren`. Das erste Öffnen des Fensters kostet die
Probe rund eine Viertelsekunde, danach steht der Befund für diesen Lauf fest.

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

**Version 1.5.0**, alle elf Meilensteine abgeschlossen — Fenster,
Direct2D-Darstellung, Datei per Kommandozeile, Multi-Frame-Dekodierung,
Seitenwechsel, Icon-Leiste, Rotation, Zoom, Verschieben, Einpassen und
Originalgröße, GIF-Animation, Ordnernavigation, Drag & Drop,
EXIF-Orientierung, Hintergrund-Dekodierung, Vollbild, DPI-Wechsel,
Fehlerbehandlung, Anwendungssymbol, Versionsangaben, Release-Build,
Drucken mit Seitenansicht, die Dateiregistrierung sowie die
Einstellungen für den klassischen Druckdialog.

Was bewusst nicht darin steckt — Rotation speichern, Löschen, Diashow — steht
in [PLAN.md](PLAN.md).

## Lizenz

[MIT-Lizenz mit der Bedingung „Commons Clause"](LICENSE): benutzen, verändern,
weitergeben und einsetzen darf sie jeder, privat wie im Betrieb. Verkaufen
nicht — weder die Software selbst noch ein Produkt oder einen Dienst, dessen
Wert im Wesentlichen auf ihrer Funktion beruht. Damit ist sie „source
available", nicht Open Source im Sinne der OSI, deren Definition den Verkauf
ausdrücklich einschließt.

Das Anwendungssymbol ist nicht von mir: es stammt aus dem Satz *Small & Flat*
von paomedia und ist gemeinfrei (CC0 1.0). Näheres unter „Drittmaterial" in
der [LICENSE](LICENSE).

## Testdaten

`testdata/` enthält Beispieldateien zum Prüfen der Darstellung. Sie entstehen
sämtlich aus [tools/](tools/) — `tools\mktestdaten.ps1` legt sie neu an:

| Datei | Zweck |
|---|---|
| `gross.png` | 3000×2000 — muss verkleinert eingepasst werden |
| `klein.png` | 120×80 — darf **nicht** hochskaliert werden, weder in der Anzeige noch auf dem Blatt |
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
