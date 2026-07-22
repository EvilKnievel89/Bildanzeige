# Erzeugt saemtliche Dateien in testdata neu.
#
#     tools\mktestdaten.ps1                 -- schreibt nach ..\testdata
#     tools\mktestdaten.ps1 -Ziel C:\tmp\td -- schreibt woanders hin
#
# Die C++-Erzeuger werden dafuer nach %TEMP% uebersetzt; im Arbeitsbaum bleibt
# nichts liegen. Alle vier schreiben in das laufende Verzeichnis, deshalb wird
# es fuer den Aufruf umgesetzt und danach zurueckgenommen.

param(
    [string]$Ziel = (Join-Path (Split-Path $PSScriptRoot -Parent) "testdata")
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $Ziel)) { New-Item -ItemType Directory -Path $Ziel | Out-Null }
$Ziel = (Resolve-Path $Ziel).Path

# --- Uebersetzer bereitstellen ---------------------------------------------
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "Weder cl.exe im Pfad noch vswhere.exe gefunden." }
    $vs = & $vswhere -latest -property installationPath
    $vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
    if (-not (Test-Path $vcvars)) { throw "vcvars64.bat nicht gefunden unter $vs" }
    cmd /c "call `"$vcvars`" >nul 2>&1 && set" |
        ForEach-Object { if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] } }
    Write-Output "MSVC-Umgebung aus $vs uebernommen."
}

$bau = Join-Path $env:TEMP "bildanzeige-testdaten"
if (Test-Path $bau) { Remove-Item $bau -Recurse -Force }
New-Item -ItemType Directory -Path $bau | Out-Null

$vorher = (Get-Location).Path
try {
    foreach ($quelle in "mkgif.cpp", "mkordner.cpp", "mkgrenzen.cpp", "mklangsam.cpp") {
        $name = [System.IO.Path]::GetFileNameWithoutExtension($quelle)
        Write-Output ""
        Write-Output "== $quelle =="
        Set-Location $bau
        $log = & cl.exe /nologo /EHsc /std:c++20 /O2 /W3 `
                        (Join-Path $PSScriptRoot $quelle) `
                        /Fe:"$bau\$name.exe" `
                        ole32.lib windowscodecs.lib 2>&1
        if ($LASTEXITCODE -ne 0) { $log; throw "$quelle liess sich nicht uebersetzen" }
        Set-Location $Ziel
        & "$bau\$name.exe"
        if ($LASTEXITCODE -ne 0) { throw "$name.exe fehlgeschlagen" }
    }
}
finally {
    Set-Location $vorher
}

Write-Output ""
Write-Output "== mktiff.ps1 =="
& (Join-Path $PSScriptRoot "mktiff.ps1") -Ziel $Ziel

Write-Output ""
Write-Output "== mkgrund.ps1 =="
& (Join-Path $PSScriptRoot "mkgrund.ps1") -Ziel $Ziel

# Zwei Erzeuger legen mehr an, als in testdata gehoert:
#
#   dreh2..5, dreh7, dreh8   alle acht EXIF-Orientierungen. Gebraucht werden
#                            dreh1 als Vergleichsstueck und dreh6 als der Fall,
#                            der hochkant erscheinen muss.
#   gross_a.gif, gross_b.gif zwei 13,6-MB-GIFs beidseits der Zwischenspeicher-
#                            grenze (PLAN.md, Abschnitt 5). Sie gehoeren zu
#                            einer Speichermessung, nicht zur Anzeigepruefung,
#                            und waeren 40 MB im Arbeitsbaum.
#
# Wer sie braucht, ruft den jeweiligen Erzeuger einzeln auf.
$weg = @(2,3,4,5,7,8 | ForEach-Object { Join-Path $Ziel "dreh$_.jpg" }) +
       @("gross_a.gif", "gross_b.gif" | ForEach-Object { Join-Path $Ziel $_ })
$weg = $weg | Where-Object { Test-Path $_ }
if ($weg) {
    $weg | Remove-Item -Force
    Write-Output ""
    Write-Output ("Entfernt: {0}" -f (($weg | ForEach-Object { Split-Path $_ -Leaf }) -join ", "))
}

Remove-Item $bau -Recurse -Force

Write-Output ""
$dateien = Get-ChildItem $Ziel -Recurse -File
"{0} Dateien, {1:N2} MB in {2}" -f $dateien.Count, (($dateien | Measure-Object Length -Sum).Sum/1MB), $Ziel
