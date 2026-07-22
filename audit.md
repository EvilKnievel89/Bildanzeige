# Sicherheitsprüfung

Stand: 22. Juli 2026, Version 1.3.1. Anlass: Verteilung auf RDS-Servern
(Terminalserver, mehrere Sitzungen teilen sich Speicher und Rechner). Geprüft
wurden alle vierzehn Quelldateien unter `src/`, mit Schwerpunkt auf den
bekannten Angriffswegen über präparierte Bilddateien sowie den Stellen, die
auf einem Mehrbenutzer-System besonderes Gewicht haben: Registry,
Kommandozeile, Pfadbehandlung, Speicherverbrauch.

## Ergebnis

Keine ausnutzbare Schwachstelle gefunden. Zwei Härtungen wurden ergriffen
(Abschnitte "Speicherbomben" und "Bauvorgang"), ein Hinweis betrifft allein
die Verteilung (Abschnitt "Verteilung auf RDS").

Die tragende Entscheidung ist architektonisch und war bereits getroffen: das
Programm parst keine Bilddateien selbst. Sämtliche Dekodierung läuft über WIC,
also die Codecs des Betriebssystems, die über Windows Update gepflegt werden.
Die klassischen Bild-Exploits -- manipulierte Header, Heap-Überläufe im
Decoder, Polyglot-Dateien -- treffen damit die Windows-Codecs, nicht diesen
Code; der Prozess läuft dabei laut Manifest ausdrücklich ohne erhöhte Rechte
(`asInvoker`).

## Im Einzelnen geprüft

**GIF-Kompositing** (`GifAnimator.cpp`) -- die einzige Stelle, an der eigene
Hand an Pixel gelegt wird. Die Maße der Frames kommen aus `GetSize()`, also
aus den tatsächlichen Pixeldaten, nicht aus den frei erfindbaren
`/imgdesc`-Metadaten; jede Kopie wird gegen die Leinwand geklippt. Auch mit
absichtlich widersprüchlichen Left/Top/Width-Angaben (einschließlich
UINT-Überlauf bei `left + width`) läuft nichts aus dem Puffer: schlimmstenfalls
wird ein Frame übersprungen oder `Lock` schlägt fehl.

**Druckpfad** (`Printer.cpp`) -- die Umwandlung von 32bppPBGRA nach 24bppBGR
im selben Puffer wurde nachgerechnet: die Zielzeile beginnt in jeder Zeile
nicht hinter der Quellzeile, und im Zeileninneren bleibt das Ziel hinter der
Quelle zurück; es wird nichts überschrieben, was noch zu lesen wäre. Die
Rendergröße ist auf 36 Megapixel gedeckelt.

**EXIF und Metadaten** -- gelesen wird allein die Orientierung, als Zahl,
geprüft auf 1 bis 8. Keine Zeichenkette aus Metadaten (Titel, Kommentar,
Kameraname) erreicht je die Oberfläche oder einen Pfad; der übliche Weg einer
EXIF-Injection existiert hier schlicht nicht.

**Fehlermeldungen** (`Common.cpp`) -- `FormatMessageW` läuft mit
`FORMAT_MESSAGE_IGNORE_INSERTS`; die bekannte Injection über `%1`-Einschübe in
Systemmeldungen ist damit ausgeschlossen.

**Dateizuordnung** (`FileAssociation.cpp`) -- `shell\open\command` setzt
`"%1"` in Anführungszeichen, Dateinamen mit Leerzeichen zerfallen also nicht
in Argumente. Geschrieben wird nach HKCU, nach HKLM nur bei tatsächlich
erhöhtem Token (`TokenElevation`, nicht Gruppenmitgliedschaft). Der
`UserChoice`-Zweig bleibt unangetastet. `RemoveApplication` fängt den leeren
Anwendungsnamen ab, aus dem sonst das Löschen von
`Software\Classes\Applications` würde.

**Kommandozeile und Übergaben** -- `argv[1]` geht ausschließlich in die
WIC-Dekodierung, nie in eine Shell. Das einzige `ShellExecuteW` startet eine
fest im Code stehende `ms-settings:`-Adresse. Es gibt kein `CreateProcess`,
kein `LoadLibrary`, keine unsicheren Zeichenkettenfunktionen, keine
temporären Dateien.

## Härtung 1: Speicherbomben (umgesetzt)

Die Maße eines Bildes stehen im Dateikopf und sind damit bloße Behauptung;
eingelöst würden sie erst beim Dekodieren, mit vier Byte je Bildpunkt. Eine
Datei von wenigen Kilobyte konnte so mehrere Gigabyte Speicher bestellen --
auf einem Terminalserver ist das der Speicher aller Sitzungen. Im GIF-Pfad
endete eine fehlgeschlagene Anforderung zudem in einem unbehandelten
`bad_alloc`, also einem Absturz statt einer Meldung.

Maßnahmen:

- `kMaxImagePixels` (250 Megapixel) in `Common.h`. Die Grenze lässt jeden
  echten Scan durch -- A2 bei 600 dpi liegt bei 139 -- und schneidet nur die
  Bestellungen ab, hinter denen keine Datei dieser Größe stehen kann.
- `DecodeWorker.cpp` prüft unmittelbar nach `GetSize`, bevor die behaupteten
  Maße Arbeit oder Speicher kosten, und meldet die Maße im Fehlertext.
- `GifAnimator::Load` setzt dieselbe Grenze für die Leinwand aus
  `/logscrdesc` und für jedes Einzelbild. Fällt sie, wird das GIF wie ein
  mehrseitiges Dokument behandelt; die Seiten laufen dann durch den
  Hintergrund-Thread, der die Grenze mit Meldung durchsetzt.
- `GifAnimator::DrawFrame` fordert den Pixelpuffer mit `new (std::nothrow)`
  an -- derselbe Weg wie beim Drucken, denn übersetzt wird ohne /EH -- und
  macht aus einem Fehlschlag eine Meldung statt `std::terminate`.

Nachgewiesen mit einer 28 Byte großen GIF-Datei, die 25000 x 20000 Bildpunkte
behauptet (500 Megapixel, dekodiert 2 GB): die Anzeige meldet die Maße und
bleibt bei rund 40 MB Arbeitsspeicher. Ab etwa 2 GB Bytegröße lehnt bereits
WIC selbst mit einem 32-Bit-Überlauffehler ab (0x80070216); die eigene Grenze
schließt genau das Fenster darunter. Gegenprobe: `riesig.jpg` (64 Megapixel)
wird angezeigt, `anim.gif` spielt ab, `mehrseitig.tif` blättert.

## Härtung 2: Bauvorgang (umgesetzt)

Zusätzlich zu `/guard:cf` (stand schon da, für Übersetzer und Linker) und den
Vorgaben von MSVC/x64 (DEP, ASLR, High-Entropy-VA):

- `/CETCOMPAT` meldet die EXE für den Schattenstapel der CPU an
  (Hardware-enforced Stack Protection) -- genau die Exploit-Klasse, die bei
  einem Fehler in einem Bild-Codec zum Tragen käme. Prozessoren ohne CET
  übergehen die Angabe.
- `/DEPENDENTLOADFLAG:0x800` löst die importierten DLLs nur noch aus System32
  auf; eine gleichnamige DLL neben der EXE liefe sonst mit. Alle Importe sind
  System-DLLs, es geht also nichts verloren.

Beides ist im fertigen Binary per `dumpbin /headers /loadconfig` bestätigt:
High Entropy Virtual Addresses, Dynamic base, NX compatible, Control Flow
Guard, CET compatible, Dependent Load Flag 0800.

## Verteilung auf RDS (offen, betrifft nicht den Code)

Die registrierten `shell\open\command`-Einträge zeigen auf den Pfad der EXE.
Liegt sie auf dem Server in einem Ordner, in den Benutzer schreiben dürfen,
kann eine Sitzung die EXE austauschen -- und wird fortan bei jedem
Bild-Doppelklick jeder Sitzung ausgeführt; bei einer Registrierung unter HKLM
ist das eine Persistenz für alle Benutzer. Die EXE gehört deshalb in einen
nur für Administratoren schreibbaren Ordner (etwa unter `C:\Program Files`)
und wird von dort erhöht mit `/registrieren` eingetragen.

Anzumerken ohne Handlungsbedarf: die Store-Erweiterungen (HEIF, AVIF, WebP,
JPEG XL) sind Drittanbieter-Codecs und damit zusätzliche Angriffsfläche. Auf
Windows Server fehlen sie ab Werk, und `CanDecode` registriert ihre Endungen
dann gar nicht erst -- für die Serververteilung das richtige Verhalten. Wer
sie nachinstalliert, übernimmt auch deren Pflege über den Store.
