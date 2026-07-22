# Übersetzt alle Prüfprogramme und lässt sie nacheinander laufen.
#
#     tools\pruefungen\pruefen.ps1
#     tools\pruefungen\pruefen.ps1 -Nur brechen,gifdaten
#
# Sie beantworten die Fragen, auf denen PLAN.md steht -- welche Decoder es
# gibt, in welchem Format ein Fax hereinkommt, was in einem GIF wirklich
# steht. Wer den Zahlen dort nicht traut, lässt das hier laufen und sieht
# nach. Übersetzt wird nach %TEMP%; im Arbeitsbaum bleibt nichts liegen.

param(
    [string[]]$Nur
)

$ErrorActionPreference = "Stop"

# Die Programme geben UTF-8 aus; ohne diese Zeile liest PowerShell es in der
# alten Zeichentabelle und macht aus Umlauten Kraut.
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()

$hier = $PSScriptRoot
$wurzel = Split-Path (Split-Path $hier -Parent) -Parent
$testdata = Join-Path $wurzel "testdata"
$src = Join-Path $wurzel "src"

# Reihenfolge mit Absicht: erst ob überhaupt etwas da ist, dann was es kann,
# dann die einzelnen Befunde.
$pruefungen = @(
    @{ Name = "umgebung";     Frage = "Sind Direct2D und WIC da?";                Args = @() },
    @{ Name = "decoder";      Frage = "Welche Formate kann dieser Rechner wirklich?"; Args = @() },
    @{ Name = "pixelformate"; Frage = "Womit kommen die Bilder herein?";
       Args = @("$testdata\fax.tif", "$testdata\mehrseitig.tif") },
    @{ Name = "gifdaten";     Frage = "Was steht in einem GIF, und als welcher Typ?";
       Args = @("$testdata\anim.gif") },
    @{ Name = "ablegen";      Frage = "Hält die Pfadentnahme aus einem HDROP?";  Args = @() },
    @{ Name = "brechen";      Frage = "Was nimmt WIC wirklich nicht mehr an?";
       Args = @("$testdata\gross.png", "$testdata\dreh1.jpg") },

    # Die einzige Prüfung, die Quellen der Anwendung mitübersetzt: gerechnet
    # werden soll mit genau der Funktion, die auch druckt, nicht mit einer
    # nachgebauten. Sie legt PDFs an und braucht deshalb ein Arbeitsverzeichnis.
    @{ Name = "drucken";      Frage = "Was kommt beim Drucken heraus?";
       Args = @("$testdata\gross.png", "$testdata\mehrseitig.tif")
       Quellen = @("Printer.cpp", "ImageDocument.cpp", "Common.cpp")
       Libs = @("comdlg32.lib", "winspool.lib", "gdi32.lib", "user32.lib", "shlwapi.lib") }
)

if ($Nur) { $pruefungen = $pruefungen | Where-Object { $Nur -contains $_.Name } }
if (-not $pruefungen) { throw "Keine der genannten Prüfungen gefunden." }

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "Weder cl.exe im Pfad noch vswhere.exe gefunden." }
    $vcvars = Join-Path (& $vswhere -latest -property installationPath) "VC\Auxiliary\Build\vcvars64.bat"
    cmd /c "call `"$vcvars`" >nul 2>&1 && set" |
        ForEach-Object { if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] } }
}

$bau = Join-Path $env:TEMP "bildanzeige-pruefungen"
if (Test-Path $bau) { Remove-Item $bau -Recurse -Force }
New-Item -ItemType Directory -Path $bau | Out-Null

$vorher = (Get-Location).Path
$fehler = 0
try {
    # brechen.exe legt seine Bastelstücke im laufenden Verzeichnis an und
    # räumt sie selbst wieder weg -- deshalb wird dort gearbeitet, nicht im
    # Arbeitsbaum.
    Set-Location $bau
    foreach ($p in $pruefungen) {
        $quellen = @(Join-Path $hier "$($p.Name).cpp")
        foreach ($q in $p.Quellen) { $quellen += (Join-Path $src $q) }
        $libs = @("ole32.lib", "windowscodecs.lib", "d2d1.lib", "shell32.lib") + $p.Libs
        $log = & cl.exe /nologo /EHsc /std:c++20 /W3 /utf-8 /I"$src" `
                        /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0A00 `
                        $quellen /Fe:"$bau\$($p.Name).exe" /link $libs 2>&1
        Write-Output ""
        Write-Output ("=" * 72)
        Write-Output ("  {0}  --  {1}" -f $p.Name, $p.Frage)
        Write-Output ("=" * 72)
        if ($LASTEXITCODE -ne 0) {
            $log
            Write-Output "  ÜBERSETZEN FEHLGESCHLAGEN"
            $fehler++
            continue
        }
        & "$bau\$($p.Name).exe" @($p.Args)
        if ($LASTEXITCODE -ne 0) {
            Write-Output ("  -> Rückgabewert {0}" -f $LASTEXITCODE)
            $fehler++
        }
    }
}
finally {
    Set-Location $vorher
    Remove-Item $bau -Recurse -Force
}

Write-Output ""
if ($fehler -eq 0) { Write-Output "Alle Prüfungen durchgelaufen." }
else { Write-Output "$fehler Prüfung(en) mit Beanstandung." }
exit $fehler
