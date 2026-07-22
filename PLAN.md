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

Keine externen Abhängigkeiten. Ergebnis ist eine einzelne EXE (512 KB, davon
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

Vier dieser Einträge sind bloße Anmeldungen. WebP, HEIF/AVIF, Raw und JPEG XL
führt `windowscodecs.dll` selbst, geliefert werden sie aber von Erweiterungen
aus dem Microsoft Store: `Microsoft.WebpImageExtension` bringt
`MSWebp_store.dll` mit und beansprucht im Paketmanifest unter
`windows.mediaCodec` genau die CLSID `{7693E886-51C9-4070-8419-9F70738EC8FA}`,
die Windows als „Microsoft Webp Decoder" führt. Fehlt das Paket, bleibt der
Eintrag stehen und erst das Erzeugen scheitert. Auf dieser Maschine ist JPEG XL
genau dieser Fall — siehe Abschnitt 7, „Angemeldet ist nicht vorhanden".

## 2. Bedienoberfläche

```
┌──────────────────────────────────────────────┐
│                                              │
│                                              │
│                  B I L D                     │   ← randlos, dunkler Hintergrund
│                                              │
│                                              │
├──────────────────────────────────────────────┤
│  ‹ › │ ⏮ ⏸ ⏭ │ ⊖ ⊕ [ ] 1:1 │ ↺ ↻ │ 🖶 │ ⛶  │   ← Icon-Leiste
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
5. **Drucken** — steht für sich, mit Trennlinien zu beiden Seiten. Es ist die
   einzige Funktion der Leiste, die den Bildschirm verlässt und etwas anstößt,
   das sich nicht mit dem nächsten Klick zurücknehmen lässt; als Nachbar der
   Drehknöpfe wäre es zu leicht im Vorbeigehen getroffen. Der Knopf öffnet
   zuerst die Seitenansicht, nicht den Druckdialog — Abschnitt 9.
6. **Vollbild** — die Leiste bleibt darin stehen. Sie erst einzublenden, sobald
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

Der **Drucker** ist der einzige geschlossene Umriss der Leiste — ein Blatt oben
hinein, ein flacher Kasten, ein Blatt unten heraus. Der erste Entwurf hatte
statt des unteren Blattes einen Ausgabeschlitz quer im Kasten; am Bildschirm
nachgesehen las sich das wie ein Vorhängeschloss (Bügel oben, Kasten, Balken
darin). Der Kasten ist daraufhin flacher geworden, das Blatt breiter und der
Schlitz zum heraustretenden Blatt. Kasten und dieses Blatt sind ein einziger
Umriss, dessen Unterkante dort offen bleibt, wo das Blatt hindurchtritt —
sonst läge es vor einer durchgehenden Linie und sähe angeklebt aus statt
herauskommend.

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
| `Strg`+`P` | Seitenansicht (von dort drucken) |
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
  Printer.*            Druckdialog, Einpassen aufs Blatt, Ausgabe über GDI
  PrintPreview.*       Seitenansicht: Metadatei aufs Blatt, Seitenschritte
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
    pruefen.ps1        übersetzt und startet alle Prüfprogramme
    umgebung.cpp       sind Direct2D und WIC da?             Abschnitt 1
    decoder.cpp        welche Formate kann er wirklich?      Abschnitt 1, 7
    pixelformate.cpp   womit kommen die Bilder herein?       Abschnitt 3
    gifdaten.cpp       was steht in einem GIF, welcher Typ?  Abschnitt 5
    ablegen.cpp        hält die Pfadentnahme aus HDROP?      Abschnitt 6
    brechen.cpp        was nimmt WIC nicht mehr an?          Abschnitt 7
    drucken.cpp        was kommt beim Drucken heraus?        Abschnitt 9
```

`drucken.cpp` ist die einzige Prüfung, die Quellen der Anwendung mitübersetzt
(`Printer.cpp`, `ImageDocument.cpp`, `Common.cpp`). Sie soll mit genau der
Funktion rechnen, die auch druckt, nicht mit einer nachgebauten — bei einem
Weg, den man nicht ohne Drucker nachrechnen kann, wäre eine zweite Fassung der
Rechnung wertlos.

Die Programme unter `pruefungen/` gehören nicht zur Anwendung. Sie sind der
Beleg für die Zahlen in diesem Dokument: jede beantwortet eine Frage, und wer
der Antwort nicht traut, lässt sie laufen — siehe
[tools/pruefungen/README.md](tools/pruefungen/README.md).

Kein Prüfbild ist von Hand gemalt: jedes hat einen Zweck, und der steht im Kopf
seines Erzeugers. Neu erzeugt und Byte für Byte verglichen sind 15 der 18
Dateien identisch — die Encoder arbeiten deterministisch, die Werkzeuge sind
also die tatsächliche Herkunft. Die drei Ausnahmen stehen in
[tools/README.md](tools/README.md).

### Umlaute

Quelltexte, Skripte und Ressourcen sind UTF-8, und Deutsch wird geschrieben,
wie man es schreibt: `Datei konnte nicht geöffnet werden`, nicht `geoeffnet`.
Die Umschrift war anfangs Bequemlichkeit und wurde zur Inkonsistenz, als die
neueren Dateien echte Zeichen benutzten — auf einem Knopf stand „Schließen",
im Meldungskasten daneben „geoeffnet".

Drei Stellen tragen das:

| | |
|---|---|
| `/utf-8` | für jede Übersetzungseinheit, auch in `pruefen.ps1` und `mktestdaten.ps1`. Ohne den Schalter liest MSVC die Dateien in der alten Zeichentabelle |
| Byte-Reihenfolge-Marke | nur in `.ps1` und `.rc`. Windows PowerShell 5.1 und `rc.exe` raten sonst |
| `activeCodePage` | im Manifest bereits auf UTF-8 |

Bezeichner bleiben ASCII, auch die deutschen in den Prüfprogrammen
(`PruefeWeissenGrund`, `hoehe`), und Dateinamen ebenso (`gross.png`,
`ueberbreit.png`). Eine Umstellung, die `gross.png` in `groß.png` verwandelt,
hat einen Fehler eingebaut, keinen behoben.

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
Datei konnte nicht geöffnet werden.
Das System kann den angegebenen Pfad nicht finden. (0x80070003)
```

Danach läuft die Anwendung weiter, mit leerer Fläche und ohne Maßstab im Titel.
Das ist der Fall eines abgezogenen Sticks oder einer getrennten Netzfreigabe.

### Angemeldet ist nicht vorhanden

Auf einem Windows Server 2025 scheitert das Öffnen einer WebP-Datei mit
`WINCODEC_ERR_COMPONENTINITIALIZEFAILURE` (0x88982F8B), während dieselbe Datei
auf der Entwicklungsmaschine anstandslos aufgeht. Der Code selbst ist der
Hinweis: nicht „Format unbekannt" (das wäre 0x88982F50), sondern „Decoder
eingetragen, ließ sich nicht erzeugen". Warum das so kommt, steht in
Abschnitt 1 — die Umsetzung liegt in einer Store-Erweiterung, und Windows
Server hat keinen Store. Nachrüsten lässt sie sich; die Anwendung nimmt sie
dann von selbst an, weil die Endungsliste zur Laufzeit entsteht.

**Nicht gemacht: beim Start aussortieren.** Naheliegend wäre, jeden Decoder
einmal probeweise zu erzeugen und die unbrauchbaren aus der Endungsliste zu
werfen. Gemessen, je frischer Prozess, dreimal wiederholt:

| | Aufzählen | mit Probeerzeugung |
|---|---|---|
| gesamt | 1,9 ms | 190 ms |

Fast alles davon sind die vier Paket-Codecs (HEIF 83–99 ms, WebP 33, Raw 32,
JPEG XL 29), und es lädt vier Codec-DLLs in den Prozess, die man womöglich nie
braucht. Hundertfache Kosten bei jedem Start für einen seltenen Fall — der
falsche Handel.

**Stattdessen sagt die Meldung, was fehlt.** Bei genau diesem einen Fehlercode
nennt `ImageDocument::Open` die Erweiterung samt ihrer Kennung im Store:

```
Datei konnte nicht geöffnet werden.

Der Decoder für ".jxl" ist angemeldet, lässt sich auf diesem Rechner aber
nicht erzeugen: es fehlt die JPEG XL-Bilderweiterung aus dem Microsoft Store
(Kennung 9MZPRTH5C0TB). Auf Windows Server sind diese Erweiterungen im
Auslieferungszustand nicht vorhanden.

Fehler bei der Komponenteninitialisierung. (0x88982F8B)
```

Nur bei 0x88982F8B. Bei `WINCODEC_ERR_COMPONENTNOTFOUND` ist der häufigere Fall
eine Datei, die schlicht kein Bild ist; dort wäre derselbe Hinweis falsch.

**Geprüft ohne Server.** Eine 66 Byte große Datei, die mit `FF 0A` beginnt — der
Signatur eines nackten JPEG-XL-Codestroms —, löst auf dieser Maschine genau
denselben Fehler aus, weil hier die JPEG-XL-Erweiterung fehlt. Dieselben Bytes
unter der Endung `.bild` ergeben den allgemeinen Hinweis: den Decoder wählt WIC
nach dem Inhalt, den Namen der Erweiterung nennt die Meldung nach der Endung,
und beides ist voneinander unabhängig. Dieselbe Datei im ISOBMFF-Rahmen
(`00 00 00 0C "JXL " 0D 0A 87 0A`) ergibt dagegen 0x88982F50 und **keinen**
Hinweis — diese Signatur beansprucht der Decoder gar nicht erst. Das ist
zugleich die Gegenprobe darauf, dass der Hinweis nicht zu breit greift.

## 8. Symbol, Versionsangaben und Auslieferung

### Das Anwendungssymbol

Das Symbol stammt aus dem Satz *Small & Flat* von paomedia und ist gemeinfrei
(CC0 1.0) — siehe „Drittmaterial" in der [LICENSE](LICENSE). Selbst gezeichnet
wäre es der einzige Teil des Projekts gewesen, bei dem Sorgfalt nicht
weitergeholfen hätte.

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

Ergebnis ist **eine Datei**, 523 776 Byte, davon 166 KB Symbol. Sie braucht
kein Redistributable: die CRT ist statisch eingebunden, und im Importverzeichnis
stehen nur Systembibliotheken.

| | |
|---|---|
| Eingetragene DLLs | `d2d1`, `ole32`, `shlwapi`, `shell32`, `user32`, `comctl32`, `comdlg32`, `gdi32`, `winspool.drv`, `kernel32` |
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

## 9. Drucken

### Was gedruckt wird

Die gezeigte Seite, in der Drehung der Anzeige. Zoom und Ausschnitt bleiben
außen vor: sie sind die Lupe, mit der man das Bild betrachtet, nicht das Bild.
Die Drehung dagegen wird mitgenommen — wer ein quer liegendes Foto aufrichtet,
damit es lesbar ist, will es nicht wieder quer aus dem Drucker bekommen.

Geholt wird die Seite **aus der Datei**, nicht aus der Anzeige. Zwei Gründe:
für die Texturgrenze der Grafikkarte kann das gezeigte Bild verkleinert worden
sein (Abschnitt 7), und eine `ID2D1Bitmap` in einem `ID2D1HwndRenderTarget`
lässt sich ohnehin nicht zurücklesen. Aufs Papier gehört die volle Auflösung.

Ein animiertes GIF ist **eine** Seite: seine Einzelbilder sind derselbe Vorgang
in der Zeit, keine Blätter. Gedruckt wird die komponierte Leinwand des gezeigten
Einzelbildes — die Rohframes sind Teilrechtecke und ergäben einzeln Fetzen
(Abschnitt 5). Die Wiedergabe wird vor dem Dialog angehalten: er ist modal, aber
der Zeitgeber schlägt in seiner Nachrichtenschleife weiter, und die Seite wäre
unter dem Auftrag fortgewandert.

### GDI, nicht Direct2D

Direct2D kann drucken, über `ID2D1PrintControl`. Der Weg dorthin verlangt aber
ein Gerät der Fassung 1.1 samt `ID2D1DeviceContext` und einem
`IPrintDocumentPackageTarget`; die Anzeige läuft auf 1.0 mit
`ID2D1HwndRenderTarget`. Die ganze Darstellung umzubauen, um ein Bild auf ein
Blatt zu legen, steht in keinem Verhältnis. Der Weg ist deshalb der alte:

```
ImageDocument::LoadFrame   32bppPBGRA -- derselbe Aufruf wie für den Bildschirm
  -> IWICBitmapFlipRotator Drehung der Anzeige
  -> IWICBitmapScaler      nur verkleinernd, siehe unten
  -> ein Puffer            32bppPBGRA über Weiß nach 24bppBGR, an Ort und Stelle
  -> StretchDIBits         auf das Zielrechteck
```

### Eingepasst wird nur nach unten, zentriert auf dem Blatt

Was größer ist als der bedruckbare Bereich, wird unter Wahrung des
Seitenverhältnisses hineingepasst. Was kleiner ist, bleibt klein. Ein Passbild
gehört nicht über A4 gezogen, bloß weil A4 im Gerät liegt: über die eigene
Größe hinaus vergrößert, gewinnt ein Bild nichts als Unschärfe, und wer ein
Vorschaubild ausdruckt, will es meist als Vorschaubild. Wer es doch anders
will, hakt in der Seitenansicht **Kleine Bilder vergrößern** an; dann wird wie
zuvor in beide Richtungen eingepasst.

Die eigene Größe ist dabei nicht die Punktzahl, sondern die Feinheit, die in
der Datei steht: 120 × 80 Punkte mit 96 dpi sind gut drei mal zwei Zentimeter
und werden auf einem Gerät mit 600 dpi zu 750 × 500 Gerätepunkten. Ein Scan mit
300 dpi kommt dadurch so groß heraus, wie er eingelesen wurde — das ist das
„100 %" des Papiers, und anders als der Maßstab des Bildschirms bedeutet es
dort etwas. Fehlt die Angabe oder ist sie unsinnig, gelten 96 dpi: dieselbe
Annahme, die auch WIC trifft. Sind die Werte der beiden Achsen verschieden —
selten, aber es gibt sie, etwa Fax-TIFFs mit 204 × 196 dpi —, gilt der
kleinere. Das Bild soll auf keiner Achse über seine Größe hinauswachsen, und
die Punkte quadratisch zu halten heißt, es so aufs Blatt zu legen, wie die
Anzeige es zeigt.

Zentriert wird in beiden Fällen auf dem **Blatt**, nicht im bedruckbaren
Bereich. Der ist bei den meisten Geräten unsymmetrisch — nachgemessen mit
`drucken.exe`:

| Drucker | Blatt | bedruckbar | Rand links/oben | rechts/unten |
|---|---|---|---|---|
| KX DRIVER for Universal Printing | 4961 × 7016 | 4760 × 6814 | 99 / 99 | 102 / 103 |
| OneNote (Desktop) | 4961 × 7016 | 4676 × 6814 | 142 / 100 | 143 / 102 |
| Microsoft Print to PDF | 4961 × 7016 | 4961 × 7016 | 0 / 0 | 0 / 0 |

Alle bei 600 dpi, A4. Der Unterschied ist hier klein — drei bis vier
Gerätepunkte, also etwa ein Zehntelmillimeter — aber er ist umsonst zu haben:
`(Blattmitte − PHYSICALOFFSET)` statt `(bedruckbarer Bereich)/2`, eine Zeile.
Bei Geräten mit großem Einzugsrand unten macht dieselbe Zeile Millimeter aus.
Anschließend wird ins Bedruckbare zurückgeschnitten, denn zentriert auf dem
Blatt kann heißen: zum Teil im Rand, den das Gerät nicht erreicht.

### Zwei Grenzen für den Zwischenpuffer

Gerechnet wird höchstens so fein, wie das Blatt es aufnimmt, und höchstens so
fein, wie die Quelle es hergibt. Ein kleines Bild wird also nicht hier
vergrößert, sondern von `StretchDIBits` im Treiber — das spart den Speicher für
eine Vergrößerung, die nichts hinzufügt. Zwei Obergrenzen kommen dazu:

- **600 dpi.** Darüber ist am Blatt kein Unterschied mehr zu sehen. Treiber, die
  1200 oder 2400 dpi melden, sind häufig; ohne diese Grenze legte ein A4-Auftrag
  dort ein halbes Gigabyte an, für nichts.
- **36 Millionen Punkte.** Für großes Papier: A0 bei 600 dpi wären 550
  Megapixel. A4 bei 600 dpi sind 32 Millionen — der Fall, um den es geht, passt
  mit Luft darunter.

Der Puffer wird ein einziges Mal angelegt und dient zweimal: erst kommen die
32bppPBGRA-Punkte hinein, dann werden sie **an Ort und Stelle** zu 24bppBGR
zusammengeschoben. Die Zielzeile ist nie länger als die Quellzeile, und im
Zeileninneren läuft das Ziel um ein Byte je Punkt hinter der Quelle her — es
wird also nichts überschrieben, was noch zu lesen wäre. Zeilenweise zu holen
wäre die naheliegende Alternative gewesen und ist gerade falsch: bei einem JPEG
hieße jeder Streifen, die Datei erneut zu dekodieren.

### Weißer Grund

Papier ist weiß. Ohne Unterlegen käme jede durchsichtige Stelle als **Schwarz**
heraus, denn in vormultiplizierten Werten steht dort eine Null. Über Weiß ist
die Rechnung denkbar einfach: `Wert + (255 − Alpha)`; da der Wert nie größer ist
als das Alpha, kann dabei nichts überlaufen.

### Der Dialog

`PrintDlgEx` mit `PD_RETURNDC`. Die Seitenwahl richtet sich nach der Datei:

| Datei | Dialog |
|---|---|
| Einzelbild, animiertes GIF | `PD_NOPAGENUMS \| PD_NOCURRENTPAGE` — es gibt nur eine Seite |
| Mehrseitig (TIFF) | Alle / Aktuelle Seite / Bereich, **vorgewählt: aktuelle Seite** |

Wer bei Seite 3 eines Faxes auf Drucken tippt, meint diese Seite; „Alle" ist
einen Klick entfernt, ein versehentlich ausgeworfener Stapel dagegen nicht mehr
einzusammeln.

**Unter Windows 11 kommt davon nur die Hälfte an.** Seit 22H2 fängt das System
den Aufruf ab und zeigt statt der Eigenschaftsseiten seinen eigenen, in UWP
geschriebenen Einheitsdialog — Fensterklasse `ApplicationFrameWindow`, Titel
„Drucken aus einer Win32-Anwendung". Dessen Auswahlliste „Seiten" enthält, per
UI-Automation ausgelesen, genau zwei Einträge: **Alle Seiten** und
**Benutzerdefinierter Bereich**. Für „Aktuelle Seite" gibt es dort kein
Gegenstück, `PD_CURRENTPAGE` fällt also wirkungslos aus. Die Vorwahl bleibt im
Code, weil sie unter dem alten Dialog greift und weil sie nichts kostet; wer
sie unter Windows 11 braucht, blättert in der Seitenansicht zur gewünschten
Seite — von dort geht sie als aktuelle Seite in den Auftrag.

Kopien und Sortierung übernimmt mit
`PD_USEDEVMODECOPIESANDCOLLATE` der Treiber — er kann das ungleich schneller,
denn dieselbe Seite mehrfach zu drucken hieße sonst, sie mehrfach zu dekodieren.
Löscht er das Flag, kann er es nicht; dann wird die ganze Folge wiederholt.

### Im UI-Thread, mit Bedacht

Gedruckt wird ohne Hintergrund-Thread, und solange steht das Fenster. Das
widerspricht Abschnitt 7 nur scheinbar: dort ging es ums Blättern, wo ein
Stillstand je Tastendruck unerträglich wäre. Hier ist der Dialog davor ohnehin
modal, die Arbeit danach ist durch die Auflösung des Geräts nach oben begrenzt,
und ein Auftrag, der einen Dateiwechsel überdauern könnte, bräuchte eine zweite
Buchführung über Zustände, die sich unterdessen ändern. Der Zeiger wird zur
Sanduhr; schlägt etwas fehl, räumt `AbortDoc` den halben Auftrag weg, statt
Papier mit halben Seiten auszuwerfen.

### Die Seitenansicht

`Strg`+`P` und der Druckknopf führen nicht geradewegs in den Druckdialog,
sondern zuerst in eine eigene Seitenansicht. Der Anlass ist der Einheitsdialog
von oben: sein Vorschaufeld bleibt bei **jeder** Win32-Anwendung leer und sagt
„Diese App unterstützt keine Seitenansicht" — Notepad, Notepad++ und MuseScore
ebenso. Das ist kein fehlendes Häkchen, sondern die Bauart: die Vorschau soll
gefüllt sein, *bevor* gedruckt wird, eine GDI-Anwendung erzeugt ihre Seiten aber
erst *danach*, zwischen `StartDoc` und `EndDoc`. Füllen kann das Feld nur, wer
Windows eine rückrufbare Seitenquelle übergibt (`IPrintDocumentSource`) — das
Modell der UWP-Anwendungen, für das der ganze Ausgabeweg auf XPS und Direct2D
1.1 umzustellen wäre. Der Preis stünde in keinem Verhältnis zum Gewinn.

Die eigene Ansicht ist dagegen fast geschenkt, weil `PrintPageToDC` in *jeden*
Gerätekontext zeichnet. Sie zeichnet in eine **Metadatei mit dem Drucker als
Bezugsgerät** und spielt diese ins Fenster: gerechnet wird also mit den
wirklichen Maßen des Geräts, und die Ansicht kann gar nicht von der Ausgabe
abweichen — sie ist die Ausgabe, nur kleiner.

Gezeigt wird das ganze Blatt, nicht bloß der bedruckbare Bereich: die
Aufzeichnung wird auf dessen Platz *innerhalb* des weißen Blattes gespielt, der
unbedruckbare Rand bleibt weiß stehen. Gerechnet wird mit dem Standarddrucker —
zu diesem Zeitpunkt hat noch niemand einen gewählt — und sein Name steht im
Fenstertitel, damit nicht im Verborgenen bleibt, worauf sich die Ansicht
bezieht. Wer im Druckdialog danach ein anderes Gerät nimmt, bekommt dessen Blatt.

Das Kästchen **Kleine Bilder vergrößern** steht in der Leiste dieser Ansicht
und nirgends sonst. Der übliche Platz für eine solche Angabe wäre der
Druckdialog, aber der gehört unter Windows 11 der Anwendung nicht mehr; und
eine Angabe, deren Wirkung zu sehen ist, gehört ohnehin dorthin, wo man sie
sieht. Ein Haken zeichnet die Seite neu auf — dieselbe Arbeit wie ein
Seitenwechsel, also auch dieselbe Sanduhr. Beim Schließen ist er wieder fort:
Vergrößern ist die Ausnahme, und eine Ausnahme, die sich merkt, wird zur
stillen Regel.

Gezeichnet wird mit **150 dpi**, nicht mit den 600 des Druckers: bei A4 sind das
rund 1240 × 1754 Punkte und damit mehr, als jedes Fenster zeigen kann, während
die Druckauflösung bei einem großen Scan Sekunden kostete. Feste Zahl statt
einer aus der Fenstergröße errechneten — sonst müsste bei jedem Ziehen am
Rahmen neu dekodiert werden.

#### Der Rahmen der Metadatei

`CreateEnhMetaFile` ohne `lpRect` setzt den Rahmen auf **das kleinste Rechteck
um das Gezeichnete**. Beim Abspielen füllte das Bild dann die ganze Zielfläche,
gleichgültig, wo auf dem Blatt es hingehört: eine querformatige Seite erschien
über das ganze Hochformat gezerrt, die Einpassung war unsichtbar und die
Ansicht eine Lüge. `PrintMetafileFrame` liefert deshalb den bedruckbaren
Bereich in Hundertstelmillimetern, gerechnet aus `HORZRES` und `LOGPIXELSX`
statt aus `HORZSIZE` — die ersten beiden beziehen sich sicher auf den
bedruckbaren Bereich, während `HORZSIZE` je nach Treiber auch das Blatt meint.

Bemerkenswert am Fehler ist, wie er sich versteckt hat: `drucken.exe` meldete
mit ihm **240 000 rote, 240 000 weiße und 0 unbemalte Punkte** und alle Haken
grün. Die Prüfung auf Weiß statt Schwarz war ja auch in Ordnung — nur war
nebenbei das Bild über die ganze Fläche gezerrt, und dass „unbemalt" bei einer
200 × 100 großen Vorlage auf A4 null sein soll, hätte auffallen müssen. Die
Prüfung besteht seither auf einem unbemalten Rest.

### Nachgemessen

`tools\pruefungen\pruefen.ps1 -Nur drucken` schickt echte Aufträge durch
`PrintPageToDC`, dieselbe Funktion, die auch die Anwendung aufruft, und prüft
je Seite fünf Dinge: Seitenverhältnis erhalten, der Maßstab ist der erwartete
(eingepasst, aber höchstens auf die eigene Größe), eine Kante stößt an den Rand
oder ringsum bleibt Rand — je nachdem, welche der beiden Grenzen gegriffen hat
—, vollständig im bedruckbaren Bereich, Mitte des Bildes auf der Mitte des
Blattes. Jede Datei läuft zweimal durch, mit und ohne Vergrößerung. Alle Seiten
von `gross.png`, `klein.png` und `mehrseitig.tif`, letztere um 90 Grad gedreht,
bestehen.

`klein.png` ist der Fall, um den es geht: 120 × 80 Punkte mit 96 dpi kommen auf
„Microsoft Print to PDF" (600 dpi, A4) als 750 × 500 Gerätepunkte mitten aufs
Blatt — mit dem Haken dagegen als 4961 × 3307, also über die ganze Breite.

Der weiße Grund ist der Fall, den man nicht sieht, wenn man nur ins PDF schaut.
Geprüft wird er über eine Metadatei mit dem Drucker als Bezugsgerät —
`PrintPageToDC` rechnet also mit den echten Maßen —, die hinterher in eine
Bitmap mit magentafarbenem Grund zurückgespielt wird. Eine zur Hälfte
durchsichtige Vorlage von 200 × 100 Punkten — mit Vergrößerung gezeichnet, denn
in ihrer eigenen Größe wäre sie auf dem Blatt eine Briefmarke, in der sich
schlecht Punkte zählen lassen — ergibt auf A4:

| | Punkte |
|---|---|
| rot (deckende Hälfte) | 84 600 |
| weiß (durchsichtige Hälfte) | 84 600 |
| schwarz | **0** |
| unbemalt (magenta) | 310 800 |

Beide Hälften gleich groß, das Durchsichtige weiß unterlegt statt schwarz — und
der unbemalte Rest belegt, dass die Einpassung erhalten bleibt und das Bild
nicht über das Blatt gezerrt wird. Die Zahlen gehen auf: 600 × 800 abgespielte
Punkte, davon 600 × 300 für ein Bild im Verhältnis 2 : 1 über die volle Breite.

## 10. Meilensteine

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
| M9 | Drucken: Dialog, Einpassen aufs Blatt, Seitenbereich | **fertig** |
| M10 | Seitenansicht vor dem Drucken | **fertig** |

Nach M1–M2 ist der genannte Kernzweck erfüllt; M3–M4 liefern die Icon-Leiste
aus der Anforderung; M5–M8 machen daraus einen Alltagsbetrachter. Mit M8 war
v1.0.0 vollständig; M9 ergab v1.1.0, M10 ergibt v1.2.0.

## 11. Bewusst nicht in v1

Rotation in die Datei speichern, Löschen, Diashow, Registrierung als
Standardanwendung für Bildformate. Alles Schreiboperationen auf Nutzerdateien
oder auf der Registrierung — sinnvoll erst, wenn die Anzeige stabil steht.

Das Drucken stand hier ebenfalls, aus demselben Grund: es galt als
Schreiboperation. Bei näherem Hinsehen ist es keine — die Datei bleibt
unberührt, geschrieben wird auf Papier. Es ist mit M9 nachgereicht (Abschnitt 9).
