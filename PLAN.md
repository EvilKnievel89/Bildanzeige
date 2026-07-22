# Bildanzeige — Planung

Schlanker Bildbetrachter für Windows im Geist der alten *Windows Bild- und Faxanzeige*:
Fenster zeigt das Bild, darunter eine Icon-Leiste für die häufigen Handgriffe.

## 1. Technische Basis

| Baustein | Wahl | Begründung |
|---|---|---|
| Sprache | C++20 | |
| Compiler | MSVC 19.4x (VS 2022 Community) | vorhanden |
| Build | CMake + Ninja | vorhanden |
| Fenster/Eingabe | Win32 API | keine Abhängigkeit, sofortiger Start |
| Dekodierung | WIC (`windowscodecs`) | Multi-Frame, EXIF, alle Formate nativ |
| Rendering | Direct2D (`d2d1`) | GPU-Skalierung, saubere Transformationen |
| COM-Handles | `Microsoft::WRL::ComPtr` (`<wrl/client.h>`) | Teil des SDK |

Keine externen Abhängigkeiten. Ergebnis ist eine einzelne EXE (437 KB, davon
166 KB Anwendungssymbol), portabel — siehe Abschnitt 8.

### Verifizierte Umgebung

Auf dieser Maschine bereits geprüft (Testkompilat gebaut und ausgeführt):

- Visual Studio Community 2022, Windows SDK 10.0.26100
- `D2D1CreateFactory` → `S_OK`, `CLSID_WICImagingFactory` → `S_OK`
- 14 WIC-Decoder registriert:

| Decoder | Erweiterungen |
|---|---|
| TIFF | `.tif .tiff` — **Multi-Page** |
| GIF | `.gif` — Multi-Frame |
| JPEG / PNG / BMP | `.jpg .jpeg .jfif .png .bmp .dib` |
| ICO / CUR | `.ico .cur` |
| HEIF | `.heic .heif .avif .avifs` |
| WebP | `.webp` |
| JPEG XL | `.jxl` |
| Raw (Microsoft) | `.cr2 .cr3 .nef .arw .orf .raf .rw2 .dng` u. a. |
| DDS, WMPhoto/JXR | `.dds .wdp .jxr` |

## 2. Bedienoberfläche

```
┌──────────────────────────────────────────────┐
│                                              │
│                                              │
│                  B I L D                     │   ← randlos, dunkler Hintergrund
│                                              │
│                                              │
├──────────────────────────────────────────────┤
│   ‹  ›  │  ⏮ ⏸ ⏭  │  ⊖ ⊕ [ ] 1:1 │ ↺ ↻ │ ⛶  │   ← Icon-Leiste
└──────────────────────────────────────────────┘
```

Gruppen von links nach rechts:

1. **Bildwechsel** — vorheriges / nächstes Bild im selben Ordner. Bleibt immer
   sichtbar, auch wenn der Ordner nur ein Bild enthält: der Bildwechsel ist der
   Hauptweg durch eine Sammlung, und ein Betrachter, der ihn erst zeigt, wenn
   es etwas zu zeigen gibt, sieht aus, als könnte er es nicht. Am Anfang und am
   Ende des Ordners wird der jeweilige Knopf grau — ohne Zähler im Fenstertitel
   ist das die einzige Stelle, an der man die Grenze überhaupt bemerkt.
2. **Seiten / Wiedergabe** — kontextabhängig, je nach geladener Datei:

   | Datei | Anzeige |
   |---|---|
   | Einzelbild (JPEG, PNG …) | ausgeblendet — Leiste bleibt aufgeräumt |
   | Mehrseitig ohne Animation (TIFF) | `⏮ ⏭` |
   | Animiert (GIF) | `⏮ ⏸ ⏭` — die Schrittknöpfe sind nur im Halt verfügbar |

   Der erste Entwurf sah vor, `⏮ ⏭` bei laufender Wiedergabe ganz auszublenden.
   Umgesetzt ist stattdessen ein Ausgrauen: die Leiste ist mittig gesetzt, ein
   Ein- und Ausblenden rückte den Wiedergabeknopf unter dem Zeiger fort, kaum
   dass man ihn getroffen hat — der nächste Klick träfe dann den falschen Knopf.
3. **Zoom** — verkleinern, vergrößern, Einpassen, Originalgröße (1:1)
4. **Drehen** — links, rechts
5. **Vollbild** — die Leiste bleibt darin stehen. Sie erst einzublenden, sobald
   sich die Maus regt, hieße einen zweiten Satz Regeln für Sichtbarkeit,
   Zeitgeber und Trefferprüfung zu führen; und ohne Rahmen und Menü ist sie
   der einzige sichtbare Rückweg. Vierzig Punkte am unteren Rand kosten auf
   einem 1440er Schirm knapp drei Prozent der Fläche.

### Icons

Als Direct2D-Pfadgeometrien im Code, nicht als Bitmap-Ressourcen. Die benötigten
Formen (Dreiecke, Kreisbögen mit Pfeilspitze, Plus/Minus, Rahmen) sind je ~10–30
Zeilen Geometrie. Vorteil: gestochen scharf auf jeder DPI-Stufe ohne mehrere
Icon-Größen, und Farbanpassung an Hell/Dunkel-Design ist ein Parameter.

Drei der Gruppen zeigen nach rechts, und sie müssen sich auf einen Blick
unterscheiden. Sie tun es über die Machart, nicht über die Größe: der
**Bildwechsel** ist ein bloßer Winkel aus Strichen, der **Seitenschritt** ein
gefülltes Dreieck mit Balken, die **Wiedergabe** ein gefülltes Dreieck ohne.

Zwei Icons bestehen aus Eckwinkeln und drohten sich ebenso zu ähneln: das
**Einpassen** setzt vier Ecken um eine leere Mitte, das **Vollbild** zwei Ecken
auf der Hauptdiagonalen, verbunden durch einen Schaft mitten hindurch. Die
gefüllte Mitte ist der Unterschied. Hinaus und zurück brauchen dabei
verschiedene Abstände: mit denselben Maßen wie für den Hinweg stünden die
Spitzen des Rückwegs einen halben Punkt auseinander, und aus zwei Pfeilen würde
ein Klecks — am vergrößerten Abzug nachgesehen und daraufhin geändert.

Die Leiste ist ein eigenes Child-Fenster mit eigenem Render-Target. Hover- und
Klick-Zustände über Hit-Testing gegen Rechtecke; Tooltips über ein
`TOOLTIPS_CLASS`-Control (liefert Verzögerung und Positionierung gratis).

### Tastatur

| Taste | Funktion |
|---|---|
| `←` / `→` | vorheriges / nächstes Bild |
| `Leertaste` | Animation anhalten / fortsetzen |
| `Bild↑` / `Bild↓` | vorherige / nächste Seite bzw. Einzelbild |
| `Pos1` / `Ende` | erste / letzte Seite |
| `Strg`+`L` / `Strg`+`R` | links / rechts drehen |
| `+` / `-`, Mausrad | Zoom |
| `Strg`+`B` | Einpassen (beste Anpassung) |
| `Strg`+`0` | Originalgröße |
| `Strg`+Pfeiltaste | Ausschnitt verschieben |
| `F11`, Doppelklick | Vollbild |
| `Esc` | Vollbild verlassen, sonst schließen |

Maus: Ziehen verschiebt das Bild bei Zoom > Fenstergröße, Rad zoomt zum Cursor.

Drei Belegungen weichen vom ersten Entwurf ab, alle um Kollisionen zu
vermeiden. `Strg`+`0` liegt auf der Originalgröße statt auf dem Einpassen, weil
diese Taste in Browsern und Editoren durchweg „100 %" bedeutet; das Einpassen
bekommt mit `Strg`+`B` das Kürzel der alten Beschriftung „Beste Anpassung".
Verschoben wird mit `Strg`+Pfeiltaste, nicht mit den blossen Pfeiltasten: die
gehören dem Bildwechsel im Ordner, und dieselbe Taste je nach Zoomstufe
verschieden zu belegen wäre nicht absehbar. Die `Leertaste` schließlich hält die
Animation an, statt wie ursprünglich vorgesehen ein Bild weiterzuschalten: dafür
gibt es bereits `→`, und ein zweites Kürzel für dieselbe Sache ist den Verzicht
auf die geläufigste Pausentaste nicht wert. Der Doppelklick auf die Bildfläche
bleibt dem Vollbild vorbehalten; über einem Knopf der Leiste zählt er dagegen
als zweiter Klick (siehe Abschnitt 4).

## 3. Aufbau

```
src/
  main.cpp             wWinMain, COM-Init, Kommandozeile
  MainWindow.*         Fensterklasse, Message-Loop, Hotkeys, Drag&Drop
  ImageDocument.*      WIC: Datei öffnen, Frames, EXIF-Orientierung
  FolderNavigator.*    Ordner scannen, natürlich sortieren, vor/zurück
  GifAnimator.*        Frame-Komposition, Disposal, Timing, Loop-Zähler
  ViewState.*          Zoom, Pan, Rotation, Fit-Modus
  RenderView.*         Direct2D-Rendertarget, Transformationen, Zeichnen
  Toolbar.*            Icon-Leiste: Geometrien, Layout, Hit-Testing
  DecodeWorker.*       Dekodierung im Hintergrund-Thread
res/
  Bildanzeige.rc       bindet Manifest, Symbol und Versionsblock ein
  Resource.h           Kennungen der Ressourcen
  icon.ico             Anwendungssymbol, neun Größen (16 bis 256)
  app.manifest.in      PerMonitorV2-DPI, Common-Controls v6, asInvoker
  Version.h.in         Versionsnummer aus CMake
```

Die beiden `.in`-Dateien tragen die Versionsnummer und werden deshalb von CMake
nach `build/generated/` erzeugt — siehe Abschnitt 8.

```
tools/
  mktestdaten.ps1      erzeugt sämtliche Dateien in testdata neu
  mkgrund.ps1          gross.png, klein.png, notiz.txt
  mktiff.ps1           mehrseitig.tif, fax.tif
  mkgif.cpp            anim.gif, spur.gif, disposal.gif, zweimal.gif
  mkordner.cpp         ordner/Bild*.png, dreh1..8.jpg
  mkgrenzen.cpp        riesig.jpg, ueberbreit.png, kaputt.png
  mklangsam.cpp        langsam.png
  pruefungen/
    pruefen.ps1        uebersetzt und startet alle Pruefprogramme
    umgebung.cpp       sind Direct2D und WIC da?            Abschnitt 1
    decoder.cpp        welche Formate kann dieser Rechner?  Abschnitt 1
    pixelformate.cpp   womit kommen die Bilder herein?      Abschnitt 3
    gifdaten.cpp       was steht in einem GIF, welcher Typ? Abschnitt 5
    ablegen.cpp        haelt die Pfadentnahme aus HDROP?    Abschnitt 6
    brechen.cpp        was nimmt WIC nicht mehr an?         Abschnitt 7
```

Die Programme unter `pruefungen/` gehören nicht zur Anwendung. Sie sind der
Beleg für die Zahlen in diesem Dokument: jede beantwortet eine Frage, und wer
der Antwort nicht traut, lässt sie laufen — siehe
[tools/pruefungen/README.md](tools/pruefungen/README.md).

Kein Prüfbild ist von Hand gemalt: jedes hat einen Zweck, und der steht im Kopf
seines Erzeugers. Neu erzeugt und Byte für Byte verglichen sind 15 der 18
Dateien identisch — die Encoder arbeiten deterministisch, die Werkzeuge sind
also die tatsächliche Herkunft. Die drei Ausnahmen stehen in
[tools/README.md](tools/README.md).

### Zuständigkeiten

**`ImageDocument`** hält den `IWICBitmapDecoder` offen und lädt Frames einzeln
nach. `GetFrameCount()` liefert die Seitenzahl. Jeder Frame läuft durch einen
`IWICFormatConverter` nach `32bppPBGRA` — nötig, weil Fax-TIFFs typischerweise
1 bpp (CCITT G4) sind und Direct2D die nicht direkt annimmt. Die
EXIF-Orientierung wird über den Metadata-Reader gelesen und als Startdrehung
gesetzt — siehe Abschnitt 6.

**`FolderNavigator`** sortiert mit `StrCmpLogicalW` (shlwapi), damit `Bild2` vor
`Bild10` steht — die naive lexikografische Sortierung ist hier ein sichtbarer
Makel. Siehe Abschnitt 6.

**`GifAnimator`** komponiert die Teilrechtecke eines GIFs auf eine Leinwand in
`logscrdesc`-Größe und gibt sie als fertige `IWICBitmap` heraus — siehe
Abschnitt 5.

**`DecodeWorker`** hält einen Thread, der vollständige Aufträge aus Pfad und
Seitennummer annimmt und die fertige `IWICBitmap` herausgibt — siehe
Abschnitt 7. Er öffnet die Datei selbst; zwischen den Threads wandert kein
WIC-Objekt, es wird nur übergeben.

**`ViewState`** hält Rotation (0/90/180/270), Zoom und Pan. Rotation wirkt rein
in der Anzeige; Dateien werden nicht verändert. Gerechnet wird in den Maßen der
**Datei**, nicht in denen der Textur: musste für die Texturgrenze verkleinert
werden, zieht `RenderView` das Bitmap beim Zeichnen wieder auseinander, und
„Originalgröße" bedeutet weiterhin, was in der Datei steht.

**`RenderView`** setzt eine Transformationskette Rotation → Skalierung →
Zentrierung/Pan und zeichnet. Beim Verkleinern wird linear interpoliert, sonst
rauschen feine Raster; oberhalb der Originalgröße dagegen Nearest-Neighbor, denn
bei einem Scan will man sehen, was dasteht, nicht ein weichgezeichnetes Mittel
daraus. An einem 1-bpp-Fax bei 424 % ist der Unterschied nachgeprüft: harte
Stufen, keine grauen Zwischenpixel.

## 4. Fallstricke, die im Entwurf berücksichtigt sind

- **Threading.** RAW- und große TIFF-Dateien brauchen Sekunden. Dekodierung
  gehört auf einen Worker-Thread, sonst friert das Fenster ein. Der Worker
  liefert eine geräteunabhängige `IWICBitmap`; die `ID2D1Bitmap` entsteht erst
  im UI-Thread, da Direct2D-Ressourcen threadgebunden sind. Worker
  initialisiert COM als MTA. Siehe Abschnitt 7.
- **Maximale Texturgröße.** `ID2D1RenderTarget::GetMaximumBitmapSize()` liegt je
  nach GPU bei 8k–16k. Großformatige Scans darüber müssen per
  `IWICBitmapScaler` heruntergerechnet werden, sonst schlägt die Bitmap-Erzeugung
  fehl. Das geschieht im Worker — gerade bei einem solchen Bild ist das
  Herunterrechnen selbst die Arbeit, die das Fenster stehen ließe. Der Maßstab
  bezieht sich trotzdem weiter auf die Maße der Datei.
- **Verlorenes Render-Target.** Bei GPU-Reset oder Treiberwechsel liefert
  `EndDraw()` `D2DERR_RECREATE_TARGET`; Target und alle Bitmaps müssen neu
  erzeugt werden. Ohne Behandlung bleibt das Fenster schwarz. Die WIC-Quelle
  wird dafür **nicht** vorgehalten: bei einem 64-Megapixel-Bild wäre das
  dauerhaft eine viertel Gigabyte für einen Fall, der jahrelang ausbleiben
  kann. Stattdessen wird das Bild neu angefordert, auf demselben Weg wie sonst.
- **Heterogene TIFF-Frames.** Seiten einer Datei können unterschiedliche Größe
  und Bittiefe haben. Fit-Berechnung darf nicht auf Frame 0 zwischenspeichern.
- **Animierte GIFs.** Frames sind Teilrechtecke, keine fertigen Bilder — siehe
  Abschnitt 5. Direktes Anzeigen einzelner Frames ergibt springende Fragmente.
- **DPI.** Per-Monitor-V2 im Manifest, `WM_DPICHANGED` behandeln — sonst ist die
  Leiste auf einem zweiten Monitor unscharf oder falsch dimensioniert.
- **Doppelklick auf die Leiste.** Bei zwei schnellen Klicks ersetzt Windows den
  zweiten `WM_LBUTTONDOWN` durch `WM_LBUTTONDBLCLK`. Wird der nicht behandelt,
  geht der zweite Klick verloren — zweimal rasch auf „drehen" ergäbe 90 statt
  180 Grad. Über einem Knopf wird der Doppelklick deshalb wie ein gewöhnlicher
  Tastendruck weitergereicht.
- **Ausschnitt beim Zoomen.** Der Ausschnitt wird als Bildpunkt in der Mitte
  geführt, nicht als Verschiebung in Fensterpixeln. Sonst wandert das Bild beim
  Zoomen unter dem Zeiger fort und beim Fenstervergrößern aus dem Blick.
- **Bildwechsel darf die Ansicht nicht zurücksetzen.** Eine neue TIFF-Seite kann
  anders groß sein und fängt deshalb eingepasst an; das nächste Einzelbild einer
  Animation liegt dagegen auf derselben Leinwand. Würde dort ebenso
  zurückgesetzt, verlöre man beim Weiterschalten den Zoom. `RenderView`
  unterscheidet das an der Bildgröße, statt es dem Aufrufer zu überlassen.
- **Wiedergabe- und Schrittknopf.** Beide zeigten zunächst dasselbe Dreieck und
  standen unmittelbar nebeneinander — man sah nicht, welcher was tut. Die
  Schrittknöpfe bekommen deshalb den Balken der geläufigen `⏮`/`⏭`-Marke.
- **Titel nach dem Start der Wiedergabe.** Der Fenstertitel wird beim Anzeigen
  eines Einzelbildes gesetzt, die Wiedergabe startet erst danach. Er trug
  deshalb die Bildzählung des Halts (`[Bild 1/5]`) durch die ganze laufende
  Animation. Beim Öffnen von der Kommandozeile fiel es nicht auf, weil das
  nächste `WM_SIZE` den Titel gleich wieder richtigstellte; erst beim Wechsel
  auf ein GIF aus dem Ordner blieb er stehen.
- **`Strg` und Pfeiltaste ohne Ausschnitt.** Verschoben wird mit `Strg` und
  Pfeiltaste — aber nur, wenn es überhaupt etwas zu verschieben gibt. Sonst
  fällt der Tastendruck durch, und `Strg`+`→` wechselte unversehens die Datei.
  Die Pfeiltasten prüfen deshalb selbst, ob `Strg` gedrückt ist.
- **Aufblitzender Hintergrund beim Bildwechsel.** Sobald die Dekodierung
  nebenher läuft, ist die Anzeige für einen Moment ohne Bild. Sie dabei sofort
  zu leeren hieße, bei jedem Tastendruck den leeren Hintergrund aufblitzen zu
  lassen — für ein gewöhnliches Foto, das nach dreißig Millisekunden da ist,
  wäre das schlimmer als das Warten. Das vorige Bild bleibt deshalb 150 ms
  stehen; erst was länger braucht, tritt überhaupt als Warten in Erscheinung.
- **Startdrehung vor dem Bild.** Die EXIF-Drehung darf nicht schon beim Öffnen
  gesetzt werden: sie träfe für einen Augenblick noch das vorige Bild, das
  sichtbar kippte. Sie wartet auf ihr Bild und wird genau einmal eingelöst —
  eine Drehung von Hand soll den Seitenwechsel überdauern.
- **Angesteuerte gegen gezeigte Seite.** Solange ein Bild unterwegs ist, sind
  beide verschieden. Geführt wird die **angesteuerte**, sonst rechnete ein
  zweiter Tastendruck während des Ladens noch von der alten Seite aus und
  bliebe wirkungslos.
- **Maßstab im Titel während des Ladens.** Er gehört zum vorigen Bild und
  stünde neben dem Namen des neuen. Während ein Auftrag läuft, bleibt er fort.

## 5. GIF-Animation

Animierte GIFs werden abgespielt, nicht als Seiten durchgeblättert. Das ist
aufwendiger als bei TIFF, weil ein GIF-Frame **kein vollständiges Bild** ist.

### Verifizierter Befund

Mit dem WIC-Encoder wurde eine 3-Frame-Testdatei erzeugt und zurückgelesen.
Ergebnis (alle Metadatenpfade bestätigt, Schreiben und Lesen je `S_OK`):

```
logische Leinwand  /logscrdesc/Width,Height    = 64 x 64
frame 0  GetSize = 64x64   Delay=50  Disposal=2  Left=0   Top=0
frame 1  GetSize = 32x32   Delay=25  Disposal=2  Left=8   Top=8
frame 2  GetSize = 32x32   Delay=100 Disposal=2  Left=20  Top=20
```

`GetFrameCount()` liefert 3. Entscheidend: **`GetSize()` gibt 32×32 zurück, nicht
64×64.** Jeder Frame ist ein Teilrechteck, das an `/imgdesc/Left,Top` auf eine
Leinwand in Größe von `/logscrdesc` komponiert werden muss.

### Datentypen der Metadaten

| Pfad | Typ | Bedeutung |
|---|---|---|
| `/logscrdesc/Width`, `/Height` | `VT_UI2` | Größe der Leinwand |
| `/logscrdesc/BackgroundColorIndex` | `VT_UI1` | Hintergrundfarbe |
| `/imgdesc/Left`, `/Top`, `/Width`, `/Height` | `VT_UI2` | Lage des Frames |
| `/grctlext/Delay` | `VT_UI2` | Anzeigedauer in **1/100 s** |
| `/grctlext/Disposal` | `VT_UI1` | 0/1 = stehenlassen, 2 = auf Hintergrund, 3 = auf vorherigen Stand |
| `/grctlext/TransparencyFlag` | `VT_BOOL` | Transparenz aktiv |
| `/appext/Application` | `VT_UI1\|VT_VECTOR` | `NETSCAPE2.0` bei Loop-Erweiterung |
| `/appext/Data` | `VT_UI1\|VT_VECTOR` | Loop-Zähler |

### Fallstricke

- **Disposal 3 („restore to previous")** verlangt eine Kopie der Leinwand *vor*
  dem Zeichnen des Frames. Ohne diese Sicherung ist Rückwärtsschritt oder
  Sprung nur durch Neuaufbau ab Frame 0 möglich.
- **`/appext/Data` kam als 4 Bytes zurück** (`03 01 00 00`), obwohl 5 geschrieben
  wurden — WIC entfernt den Block-Terminator. Der Loop-Zähler steht in Byte 2–3
  little-endian, `0` heißt endlos. Die Länge muss geprüft werden, statt feste
  5 Bytes anzunehmen.
- **Delay 0 oder 1** kommt in freier Wildbahn massenhaft vor und würde eine
  Vollgas-Schleife bedeuten. Konvention der Browser: Werte < 2 auf 10 (100 ms)
  anheben.
- **Zeitdrift.** `SetTimer` mit der Frame-Dauer zu verketten läuft systematisch
  nach, weil die Timer-Auflösung bei ~15,6 ms liegt. Stattdessen absolute
  Zielzeitpunkte über `QueryPerformanceCounter` aufsummieren.
- **Pausieren, wenn unsichtbar.** Bei minimiertem Fenster den Timer anhalten,
  sonst läuft die Animation im Leerlauf weiter.
- **Wiedergabe am Ende neu starten.** Wird sie gestartet, während das letzte
  Einzelbild steht, zählt der Umbruch beim ersten Takt sofort als vollendeter
  Durchlauf — ein GIF mit zwei vorgesehenen Wiederholungen liefe dann nur
  einmal. Deshalb setzt der Start dort auf Frame 0 zurück, wie ein Abspielgerät.

### Umsetzung

`GifAnimator` führt eine laufende Leinwand in `logscrdesc`-Größe und komponiert
die Frames fortlaufend darauf: Aufräumregel des vorigen Frames anwenden, dann
den nächsten überlagern. Vorwärts ist das die einzige Reihenfolge, in der die
Regeln überhaupt definiert sind; ein Rücksprung spielt ab Frame 0 nach.

Der erste Entwurf sah vor, beim Laden **alle** Frames vorzukomponieren. Umgesetzt
ist stattdessen ein Zwischenspeicher, der sich nebenbei füllt: jede fertige
Leinwand wird aufgehoben, solange das Budget reicht. Das Öffnen bleibt dadurch
sofort, auch bei einem langen GIF, und es wird nur so viel Speicher belegt, wie
auch wirklich gezeigt wurde. Der erste Durchlauf komponiert, ab dem zweiten wird
nur noch abgerufen.

Das Budget ist `Breite × Höhe × 4 × Frames ≤ 256 MB`. Darüber entfällt der
Zwischenspeicher ganz; es bleibt bei der einen laufenden Leinwand, und ein
Rücksprung kostet dauerhaft einen Neuaufbau.

Nachgemessen an zwei GIFs gleicher Bauart (3400 × 1000, also 13,6 MB je
Leinwand) beidseits der Grenze:

| Datei | Frames | Rechnung | Arbeitsspeicher |
|---|---|---|---|
| `gross_a.gif` | 19 | 258 MB — darunter | **318 MB** (zwischengespeichert) |
| `gross_b.gif` | 20 | 272 MB — darüber | **71 MB** (fortlaufend) |

Ein Frame Unterschied kippt das Verhalten, und beide Wege liefern dasselbe Bild.

Rotation und Zoom greifen als Direct2D-Transformation an der fertigen Leinwand
an, gelten also unverändert auch während der Wiedergabe — es braucht dafür
keinen Sonderweg. Nachgeprüft: nach `Strg`+`L` bleibt die Drehung über den
Bildwechsel hinweg stehen, und ein Zoom auf 195 % übersteht sowohl den
Einzelschritt als auch das Fortsetzen der Wiedergabe.

### Takt

Die Frame-Dauer wird als **absoluter Zielzeitpunkt** über
`QueryPerformanceCounter` aufsummiert; `SetTimer` bekommt jeweils nur die
verbleibende Zeit. Ist die Wiedergabe zurückgefallen — das Fenster wurde
gezogen, das System war beschäftigt — wird der Takt neu angesetzt, statt die
versäumten Einzelbilder im Schnelldurchlauf nachzuholen; das sähe aus wie ein
Ruckler mit Anlauf. Im Symbolzustand hält der Takt ganz an.

## 6. Ordner, Ablegen und EXIF

### Die Ordnerliste

Beim Öffnen wird der Ordner der Datei eingelesen und natürlich sortiert. Welche
Endungen zählen, kommt nicht aus einer festen Liste im Code, sondern von den
tatsächlich registrierten Decodern: `CreateComponentEnumerator(WICDecoder, …)`
und `IWICBitmapCodecInfo::GetFileExtensions`. Ist auf einem Rechner HEIF oder
ein Raw-Format nachinstalliert, steht es damit von selbst in der Liste, ohne
dass am Programm etwas zu ändern wäre. Schlägt die Aufzählung fehl, greift eine
kleine Notliste der immer vorhandenen Formate.

Die Liste ist eine **Momentaufnahme**. Sie bei jedem Bildwechsel neu einzulesen
wäre bei einem Ordner mit mehreren tausend Dateien spürbar, und während man
Bilder ansieht ändert er sich in aller Regel nicht. Was seither doch
verschwunden ist, fällt beim Ansteuern auf: existiert die Nachbardatei nicht
mehr, wird sie stillschweigend übergangen und die nächste versucht. Eine Meldung
über eine Datei, die der Benutzer gerade selbst weggeräumt hat, hülfe ihm nicht.
Ein tatsächlich kaputtes Bild meldet sich dagegen sehr wohl — davon will man
wissen.

Anders als die Wiedergabe läuft der Ordner **nicht um**: am Ende bleibt es
stehen. Die Regel lautet damit durchgehend „blättern begrenzt, Wiedergabe läuft
um", und der graue Knopf sagt, wo Schluss ist.

Wird statt einer Datei ein **Ordner** übergeben oder abgelegt, wird das erste
Bild darin gezeigt. Das ist derselbe Weg für Kommandozeile und Ablegen, weil
beides in `OpenFile` zusammenläuft.

### Ablegen

Das Fenster trägt `WS_EX_ACCEPTFILES` und behandelt `WM_DROPFILES`. Bei mehreren
Dateien zählt die erste; die übrigen liegen im selben Ordner und stehen damit
ohnehin schon auf den Pfeiltasten.

Der Ablegevorgang selbst lässt sich von außen nicht auslösen: die Shell legt das
`HDROP` im Zielprozess an, ein Handle aus einem fremden Prozess ist dort
wertlos. Geprüft wurde deshalb der Teil mit echtem Risiko — die Größenrechnung
der Puffer. `DragQueryFileW` meldet die Länge **ohne** Abschluss, verlangt aber
die Puffergröße **mit**.

### EXIF-Orientierung

Ein hochkant gehaltenes Foto steht quer in der Datei und trägt die Drehung nur
als Vermerk: EXIF-Tag 274. Wo die IFD hängt, entscheidet der Container — JPEG
und HEIF legen sie in den APP1-Block (`/app1/ifd/{ushort=274}`), TIFF hat sie
unmittelbar (`/ifd/{ushort=274}`). Beide Pfade werden versucht, statt den
Containertyp abzufragen; ein fehlender Pfad kostet nur einen fehlgeschlagenen
Aufruf.

Gelesen wird an Frame 0, und der Wert gilt für die **ganze Datei**. Ließe man
jede Seite ihre eigene Startdrehung durchsetzen, überstünde eine Drehung von
Hand den Seitenwechsel nicht mehr — und genau das soll sie (Abschnitt 5).

| Wert | Bedeutung | Umgesetzt als |
|---|---|---|
| 1 | normal | 0° |
| 2 | waagerecht gespiegelt | 0° |
| 3 | um 180° gedreht | 180° |
| 4 | senkrecht gespiegelt | 180° |
| 5 | gespiegelt + 270° | 90° |
| 6 | um 90° gedreht | 90° |
| 7 | gespiegelt + 90° | 270° |
| 8 | um 270° gedreht | 270° |

Von den vier gespiegelten Fällen (2, 4, 5, 7) wird nur der **Drehanteil**
übernommen. Sie entstehen aus Kameras praktisch nie, sondern aus
Bildbearbeitung, die das Spiegeln in die Metadaten geschrieben hat; eine
Spiegelung dafür einzuführen hieße, sie durch die ganze Ansicht zu ziehen —
`ViewState`, `RenderView` und jede Umrechnung von Fenster- in Bildpunkte. Werte
außerhalb von 1..8 gelten als kaputt und werden ignoriert; dann lieber ungedreht
zeigen, als das Bild auf gut Glück zu kippen.

Die Drehung ist die **Ausgangslage**, auf die sich ein späteres Drehen von Hand
bezieht. Sie wird beim Dateiwechsel neu gesetzt, gilt also je Datei.

### Nachgemessen

Die Testdateien sind so gebaut, dass ein Fehler zählbar wird. `ordner/Bild1`,
`Bild2` und `Bild10` tragen 1, 2 bzw. 10 weiße Quadrate; gezählt wurden 992,
1984 und 9920 weiße Bildpunkte — das exakte Verhältnis 1 : 2 : 10 und damit die
natürliche Reihenfolge, nicht die lexikografische (1, 10, 2).

Für die EXIF-Tabelle liegt in `dreh1.jpg` … `dreh8.jpg` ein weißer Block in der
gespeicherten linken oberen Ecke eines 1200 × 300 großen Bildes. Gemessen wird,
in welchem **Viertel** der Bildfläche er landet und welchen Maßstab der Titel
zeigt — das Querformat passt bei 54 %, das Hochformat bei 33 % ins Fenster.
Alle acht Werte trafen die Vorhersage: 1 und 2 oben links bei 54 %, 3 und 4
unten rechts bei 54 %, 5 und 6 oben rechts bei 33 %, 7 und 8 unten links bei
33 %.

## 7. Hintergrund, Vollbild und DPI

### Der Hintergrund-Thread

Aufgetragen wird immer ein **vollständiger** Auftrag: Pfad, Seitennummer,
Texturgrenze. Der Worker öffnet die Datei selbst, dekodiert, verkleinert
gegebenenfalls und gibt eine fertige `IWICBitmap` heraus. Es wandert damit kein
WIC-Objekt zwischen den Threads hin und her, es wird nur übergeben — die Frage,
ob zwei Apartments sich einen Decoder teilen dürfen, stellt sich gar nicht
erst. Der Preis ist, dass die Datei zweimal geöffnet wird: einmal im UI-Thread
für Seitenzahl und EXIF, einmal im Worker für die Pixel. Das Öffnen liest nur
den Dateikopf; die Arbeit, um die es geht, fällt so oder so nur einmal an.

Es wartet höchstens **ein** Auftrag. Wer rasch durch einen Ordner blättert,
will das letzte Bild sehen, nicht jedes dazwischen. Jeder Auftrag trägt eine
Marke; trifft ein Ergebnis mit veralteter Marke ein, wird es verworfen, und
schon der Worker meldet gar nicht erst, was während der Arbeit überholt wurde.

Nachgemessen an `langsam.png` (60000 × 2000, 120 Megapixel) gegen den Stand von
M6, mit `SendMessageTimeout(WM_NULL)` als Frage, ob das Fenster noch antwortet:

| | M6 | M7 |
|---|---|---|
| längster Stillstand | 301–327 ms | **0 ms** |
| Titel quittiert den Tastendruck | 330–337 ms | **0,7 ms** |
| Bild steht | 330–337 ms | 330–337 ms |
| Arbeitsspeicher | 54 MB | 53 MB |

Die Gesamtdauer ändert sich also nicht — sie verlagert sich nur dorthin, wo sie
nicht mehr im Weg steht.

**Was im UI-Thread bleibt.** Das Hochladen zur Grafikkarte: Direct2D-Ressourcen
gehören dem Thread, der sie erzeugt hat. Bei `langsam.png` fällt das nicht ins
Gewicht, weil die Textur nach dem Verkleinern nur 34 MB misst. Bei
`riesig.jpg` (8000 × 8000, 256 MB Textur) sind es dagegen **54–68 ms**, die
bleiben — von zuvor 141–169 ms. Das ließe sich nur mit einer
`MULTI_THREADED`-Fabrik verschieben, und dafür müsste der Worker das
Render-Target in die Hand bekommen, das ihm unter den Händen verlorengehen
kann. Für zwei Zehntel Sekunde bei einem 64-Megapixel-Bild ist das die
Verflechtung nicht wert.

### Der Ladezustand

Sobald nebenher dekodiert wird, ist die Anzeige für einen Moment ohne Bild. Sie
sofort zu leeren hieße, bei jedem Tastendruck den leeren Hintergrund aufblitzen
zu lassen. Das vorige Bild bleibt deshalb **150 ms** stehen; erst danach wird
geleert und der Titel sagt „wird geladen …". Gemessener Verlauf:

| Datei | Titel quittiert | Ladezustand | Bild steht |
|---|---|---|---|
| `langsam.png`, 120 Mpx | 10,8 ms | **158,1 ms** | 359,9 ms |
| `klein.png`, 120 × 80 | 1,4 ms | *tritt nie ein* | 2,9 ms |

Während des Ladens wurden 0 Bildpunkte in der Bildfläche gezählt, danach 6627 —
die Fläche ist also wirklich leer und nicht nur beschriftet.

Der Mauszeiger sagt es noch vor der Frist: er wechselt auf `IDC_APPSTARTING`,
sobald der Auftrag ergeht. Windows bestimmt ihn allerdings erst bei der
nächsten Mausbewegung neu, und wer per Tastatur blättert, bewegt sie nicht —
die Frage wird deshalb einmal von Hand gestellt.

### Maßstab und Texturgrenze

Die Ansicht rechnet in den Maßen der **Datei**. Musste für die Texturgrenze
verkleinert werden, zieht Direct2D das Bitmap beim Zeichnen wieder auseinander.
An `ueberbreit.png` (20000 × 400, Grenze 16384) bei 884 Punkten Bildfläche:

| | M6 | M7 |
|---|---|---|
| Titel | 5 % | **4 %** |
| Rechnung | 884 / 16384 = 5,40 % | 884 / 20000 = 4,42 % |

Auf dem Schirm steht in beiden Fällen dasselbe Bild — nur bedeutet die Zahl
jetzt, was sie überall sonst auch bedeutet. Damit ist die aus M4 offene
Einschränkung erledigt. Geglättet wird nun ebenfalls nach der Textur bemessen:
oberhalb der Originalgröße bleiben die Pixel nur dann hart, wenn die Textur die
Pixel der Datei auch wirklich enthält. Bei einem verkleinerten Großformat gibt
es dort nichts mehr zu zeigen, und harte Kanten vergrößerten bloß die Stufen
des Herunterrechnens.

### Vollbild

`F11`, Doppelklick auf die Bildfläche und der Knopf ganz rechts führen in beide
Richtungen; `Esc` verlässt zuerst das Vollbild und schließt erst danach das
Fenster. Ohne Rahmen erinnert sonst nichts daran, dass darunter noch ein
Fenster liegt.

Umgesetzt als Wegnahme von `WS_OVERLAPPEDWINDOW` und `SetWindowPos` auf
`rcMonitor` — die Taskleiste wird mit überdeckt. Zurück geht es über
`SetWindowPlacement`: das stellt auch wieder her, was vor dem Vollbild
maximiert war, was eine gemerkte Rechteckgröße nicht könnte.

Nachgemessen auf einem 2560 × 1440-Schirm, alle vier Wege einzeln: Fenster
900 × 640 mit Rahmen → 2560 × 1440 ohne → zurück auf 900 × 640 mit. Der
Einpass-Maßstab folgt mit (28 % gegen 70 %), und alle neun Knöpfe behalten
ihren Zustand.

### DPI

Das Manifest meldet PerMonitorV2. Die Leiste rechnet in DIP und multipliziert
die Stufe auf; die Bildfläche bleibt bei 96 dpi, damit ein Bildpunkt ein
Bildschirmpunkt ist — für einen Betrachter die ehrlichste Basis.

Zwei Stellen waren offen. Die **Anfangsgröße** stand fest in Pixeln und war auf
einem 150 %-Schirm nur zwei Drittel so groß wie gemeint; sie wird jetzt nach
`GetDpiForWindow` bemessen, und der Rahmen kommt über
`AdjustWindowRectExForDpi` hinzu, weil er nicht im selben Verhältnis wächst wie
die Fläche. Und `WM_DPICHANGED` fragte die Stufe am Fenster ab, statt sie der
Nachricht zu entnehmen — die Nachricht ist maßgeblich.

Geprüft, indem `WM_DPICHANGED` mit 144 dpi und einem 1,5-fachen
Vorschlagsrechteck geschickt wird. Die Gegenprobe entscheidet: mit der alten
Skalierung nachgemessen treffen die Proben daneben (`43 88 212 43 …`, also
Untergrund statt Icon), mit 1,5 sitzt das gewohnte Muster
(`212 212 88 212 85 212 212 212 212`). Leiste 40 → 60, Knopf 34 → 51.

### Fehlerbehandlung

**Geräteverlust.** `EndDraw()` meldet `D2DERR_RECREATE_TARGET`; Target, Pinsel
und Bitmap werden neu erzeugt. Die WIC-Quelle wird dafür nicht vorgehalten —
das Bild wird neu angefordert. Derselbe Weg hängt auch an `WM_DISPLAYCHANGE`,
was ihn zugleich von außen prüfbar macht: vor der Nachricht steht „27 %" im
Titel, unmittelbar danach kein Maßstab (die Bitmap ist fort, das Bild
unterwegs), 700 ms später wieder „28 %".

**Beschädigte Dateien melden sich nicht.** Der Fehlerweg des Worker-Threads
sollte an einer kaputten Datei geprüft werden — es ließ sich keine bauen. WIC
repariert, statt zu verweigern: ein abgeschnittenes JPEG (auf ein Fünftel), ein
abgeschnittenes PNG und je drei Stellen mit 64 verfälschten Bytes mitten im
Datenstrom kamen **alle sechs ohne Fehler durch**, das JPEG mit der Bemerkung
„premature end of data segment" auf der Konsole und den Pixeln obendrauf. Wer
auf `WINCODEC_ERR_BADIMAGE` wartet, wartet lange.

Was den Weg wirklich auslöst, ist ein **Pfad, der verschwindet**. Geprüft über
ein `subst`-Laufwerk: Bild öffnen, Laufwerk abmelden, Geräteverlust vortäuschen.
Der offene Griff der Anwendung bleibt gültig, das erneute Öffnen im Worker
scheitert, und die Meldung kommt an:

```
Datei konnte nicht geoeffnet werden.
Das System kann den angegebenen Pfad nicht finden. (0x80070003)
```

Danach läuft die Anwendung weiter, mit leerer Fläche und ohne Maßstab im Titel.
Das ist der Fall eines abgezogenen Sticks oder einer getrennten Netzfreigabe.

## 8. Symbol, Versionsangaben und Auslieferung

### Das Anwendungssymbol

Die `icon.ico` bringt neun Größen mit: 16, 24, 32, 48, 64, 72, 96, 128 und 256.
Eingebettet wird die ganze Datei, nicht ein einzelnes Bild — Windows sucht sich
die passende Größe selbst heraus, und was es nicht findet, rechnet es hoch. Das
sieht man einem Symbol an.

Geprüft wurde in zwei Schritten. Erst **Byte für Byte**: jedes Einzelbild aus
der `.ico` gegen die gleichnamige `RT_ICON`-Ressource der EXE.

| Nr | Größe | Bytes in der `.ico` | Bytes in der EXE | gleich |
|---|---|---|---|---|
| 1 | 256 | 4 125 | 4 125 | ja |
| 2 | 128 | 67 624 | 67 624 | ja |
| 3 | 96 | 38 056 | 38 056 | ja |
| 4 | 72 | 21 640 | 21 640 | ja |
| 5 | 64 | 16 936 | 16 936 | ja |
| 6 | 48 | 9 640 | 9 640 | ja |
| 7 | 32 | 4 264 | 4 264 | ja |
| 8 | 24 | 2 440 | 2 440 | ja |
| 9 | 16 | 1 128 | 1 128 | ja |

Dazu die Gruppe (`RT_GROUP_ICON`) mit neun Einträgen in 132 Byte.

Dann **so, wie die Shell es hergibt**: für jede Größe über `SHDefExtractIcon`
aus der EXE geholt und mit derselben Größe aus der Quelldatei verglichen — bei
16, 24, 32, 48, 64, 72, 96 und 128 kein einziger abweichender Punkt. Die 256er
bleibt bei diesem zweiten Vergleich außen vor: sie liegt als PNG in der `.ico`,
und die `Icon`-Klasse von .NET liest PNG-Einzelbilder nicht, sondern rechnet
aus einer kleineren hoch. Gegen das rohe PNG gehalten weichen 666 von 65 536
Punkten ab, **alle in halbdurchsichtigen Rändern** — der Rundungsverlust beim
Weg durch das vormultiplizierte Alpha eines Symbolgriffs, nicht der Datei
anzulasten.

**Fenstersymbole hängen an der DPI-Stufe.** Die Titelleiste will bei 150 %
nicht 16, sondern 24 Punkte. `ApplyIcons` erfragt die Größe mit
`GetSystemMetricsForDpi` und lädt aus derselben `.ico` das passende Bild;
aufgerufen wird es aus `ApplyDpi`, also bei `WM_CREATE` und bei jedem
`WM_DPICHANGED`.

| Stufe | klein | groß |
|---|---|---|
| 96 dpi (100 %) | 16×16 | 32×32 |
| 144 dpi (150 %) | 24×24 | 48×48 |

Dass dabei wirklich das mitgelieferte 24er verwendet wird und nicht ein
hochgezogenes 16er, entscheidet die Gegenprobe: gegen das echte 24er weichen
0 von 576 Punkten ab, gegen ein aufgeblasenes 16er 494.

Das Symbol der Fensterklasse wird trotzdem gesetzt — es ist das, was zu sehen
wäre, falls `WM_CREATE` nicht dazu käme, und das kleine ist dort eigens
angegeben, weil Windows es sonst aus dem großen herunterrechnet.

Bei jedem Wechsel werden zwei Symbole geladen und die beiden alten freigegeben,
und zwar **erst nach** dem Setzen der neuen. 40 Wechsel zwischen 96 und 144 dpi
lassen die Zahl der GDI-Objekte (23) und der Benutzerobjekte (19) unverändert;
ohne Freigabe wären es 80 Symbole mehr.

### Die Versionsnummer steht an genau einer Stelle

Sie stand vorher an dreien: in der `project()`-Zeile, in der `.rc` und in der
`assemblyIdentity` des Manifests. Zwei davon werden jetzt erzeugt —
`res/Version.h.in` und `res/app.manifest.in` gehen durch `configure_file` nach
`build/generated/`.

Dabei tut sich eine Falle auf: Für `.rc`-Dateien entsteht keine
Abhängigkeitsliste wie beim Übersetzen. Ohne `OBJECT_DEPENDS` bliebe nach einer
neuen Nummer die alte Ressource in der EXE stehen — lautlos. Mit der Zeile
greift es: Nummer auf 1.0.1 gesetzt, ohne Aufräumen gebaut, die EXE meldet
1.0.1.0; zurückgesetzt, wieder gebaut, 1.0.0.0.

Im Versionsblock steht neben den Nummern auch `FileDescription` — das ist der
Name, unter dem die Anwendung im Task-Manager und im Menü „Öffnen mit"
erscheint, nicht der Dateiname. `FILEFLAGS` trägt im Debug-Build `VS_FF_DEBUG`;
`rc.exe` bekommt dafür von CMake `-D_DEBUG` gereicht.

### Das Manifest

PerMonitorV2 (Abschnitt 7), Common-Controls v6 (für die Tooltips der Leiste),
`activeCodePage` UTF-8, `supportedOS` Windows 10/11 und neuerdings
`requestedExecutionLevel asInvoker`. Letzteres wäre auch die Vorgabe, aber ohne
Angabe rät Windows aus dem Dateinamen, ob ein Installationsprogramm vorliegt.

**`longPathAware` steht bewusst nicht darin.** Die eigenen Puffer wären
bereit — nirgends `MAX_PATH`, überall `std::wstring`, `DragQueryFileW` wird
nach der Länge gefragt —, aber `PathIsDirectoryW`, `PathFileExistsW`,
`PathFindFileNameW` und `PathFindExtensionW` aus shlwapi hören bei 260 Zeichen
auf. Der Schalter verspräche mehr, als der Code hält.

**Zwei Bindestriche.** Im Projekt vertritt `--` überall den Gedankenstrich; in
einem XML-Kommentar ist die Folge verboten. Der erste Entwurf des Manifests
enthielt sie, `mt.exe` meldete „Windows konnte die angeforderten XML-Daten nicht
analysieren", und die Frage war, wie laut dieser Fehler zur Laufzeit wäre.
Antwort: sehr laut. Windows verweigert den Start mit „Die
Side-by-Side-Konfiguration ist ungültig" — kein stilles Zurückfallen auf ein
Programm ohne Manifest. Das Auslesen des Manifests mit `mt.exe` gehört seitdem
zur Prüfliste.

### Der Release-Build

```bat
"…\VC\Auxiliary\Build\vcvars64.bat"
cmake --preset msvc-x64
cmake --build --preset release
```

Ergebnis ist **eine Datei**, 437 248 Byte, davon 166 KB Symbol. Sie braucht
kein Redistributable: die CRT ist statisch eingebunden, und im Importverzeichnis
stehen nur Systembibliotheken.

| | |
|---|---|
| Eingetragene DLLs | `d2d1`, `ole32`, `shlwapi`, `shell32`, `user32`, `comctl32`, `kernel32` |
| Laufzeit-DLLs | keine |
| Kennzeichen | High Entropy VA, Dynamic Base, NX, Terminal Server Aware |
| Sprungprüfung | Guard Flags `10017500`, CF instrumented |

`windowscodecs.dll` fehlt in der Liste, obwohl das ganze Dekodieren darauf
beruht: WIC wird ausschließlich über COM angesprochen
(`CoCreateInstance(CLSID_WICImagingFactory)`), es gibt also keine einzige
importierte Funktion. Geladen wird die DLL trotzdem — erst zur Laufzeit, durch
COM.

**Was gemessen und wieder ausgebaut wurde.** Optimierung über
Übersetzungseinheiten hinweg (LTCG) war eingebaut; dreimal aus derselben Quelle
gebaut:

| Fassung | Bytes | Bauzeit |
|---|---|---|
| wie ausgeliefert | 437 248 | 2,3 s |
| mit LTCG | 437 760 | 2,3 s |
| ohne `/guard:cf` | 434 688 | 2,1 s |

LTCG bringt 512 Byte **mehr** und keinen messbaren Unterschied an Bauzeit. Bei
zehn kleinen Übersetzungseinheiten, deren Arbeit ohnehin in WIC und Direct2D
stattfindet, ist nichts zu holen — der Schalter ist wieder heraus.
`/guard:cf` kostet 2 560 Byte und bleibt: das ist der Preis dafür, dass
indirekte Sprünge geprüft werden.

## 9. Meilensteine

| # | Ergebnis | Stand |
|---|---|---|
| M1 | Fenster, Direct2D, Bild per Kommandozeile öffnen, ins Fenster einpassen | **fertig** |
| M2 | Multi-Frame-Dekodierung, Seitenwechsel per Tastatur (TIFF) | **fertig** |
| M3 | Icon-Leiste: Geometrien, Layout, Hover, Klick, Tooltips, **Rotation** | **fertig** |
| M4 | Zoom, Pan, Einpassen/Originalgröße | **fertig** |
| M5 | GIF-Animation: Komposition, Disposal, driftfreies Timing, Pause/Einzelschritt | **fertig** |
| M6 | Ordnernavigation, Drag&Drop, EXIF-Orientierung | **fertig** |
| M7 | Hintergrund-Dekodierung, DPI, Vollbild, Fehlerbehandlung | **fertig** |
| M8 | Anwendungssymbol, Versionsressource, Release-Build | **fertig** |

Nach M1–M2 ist der genannte Kernzweck erfüllt; M3–M4 liefern die Icon-Leiste
aus der Anforderung; M5–M8 machen daraus einen Alltagsbetrachter. Mit M8 ist
v1.0.0 vollständig.

## 10. Bewusst nicht in v1

Rotation in die Datei speichern, Löschen, Drucken, Diashow, Registrierung als
Standardanwendung für Bildformate. Alles Schreiboperationen auf Nutzerdateien —
sinnvoll erst, wenn die Anzeige stabil steht.
