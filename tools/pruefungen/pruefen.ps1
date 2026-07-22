# Uebersetzt alle Pruefprogramme und laesst sie nacheinander laufen.
#
#     tools\pruefungen\pruefen.ps1
#     tools\pruefungen\pruefen.ps1 -Nur brechen,gifdaten
#
# Sie beantworten die Fragen, auf denen PLAN.md steht -- welche Decoder es
# gibt, in welchem Format ein Fax hereinkommt, was in einem GIF wirklich
# steht. Wer den Zahlen dort nicht traut, laesst das hier laufen und sieht
# nach. Uebersetzt wird nach %TEMP%; im Arbeitsbaum bleibt nichts liegen.

param(
    [string[]]$Nur
)

$ErrorActionPreference = "Stop"

# Die Programme geben UTF-8 aus; ohne diese Zeile liest PowerShell es in der
# alten Zeichentabelle und macht aus Umlauten Kraut.
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()

$hier = $PSScriptRoot
$testdata = Join-Path (Split-Path (Split-Path $hier -Parent) -Parent) "testdata"

# Reihenfolge mit Absicht: erst ob ueberhaupt etwas da ist, dann was es kann,
# dann die einzelnen Befunde.
$pruefungen = @(
    @{ Name = "umgebung";     Frage = "Sind Direct2D und WIC da?";                Args = @() },
    @{ Name = "decoder";      Frage = "Welche Formate kann dieser Rechner?";      Args = @() },
    @{ Name = "pixelformate"; Frage = "Womit kommen die Bilder herein?";
       Args = @("$testdata\fax.tif", "$testdata\mehrseitig.tif") },
    @{ Name = "gifdaten";     Frage = "Was steht in einem GIF, und als welcher Typ?";
       Args = @("$testdata\anim.gif") },
    @{ Name = "ablegen";      Frage = "Haelt die Pfadentnahme aus einem HDROP?";  Args = @() },
    @{ Name = "brechen";      Frage = "Was nimmt WIC wirklich nicht mehr an?";
       Args = @("$testdata\gross.png", "$testdata\dreh1.jpg") }
)

if ($Nur) { $pruefungen = $pruefungen | Where-Object { $Nur -contains $_.Name } }
if (-not $pruefungen) { throw "Keine der genannten Pruefungen gefunden." }

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
    # brechen.exe legt seine Bastelstuecke im laufenden Verzeichnis an und
    # raeumt sie selbst wieder weg -- deshalb wird dort gearbeitet, nicht im
    # Arbeitsbaum.
    Set-Location $bau
    foreach ($p in $pruefungen) {
        $quelle = Join-Path $hier "$($p.Name).cpp"
        $log = & cl.exe /nologo /EHsc /std:c++20 /W3 /utf-8 $quelle `
                        /Fe:"$bau\$($p.Name).exe" ole32.lib windowscodecs.lib d2d1.lib shell32.lib 2>&1
        Write-Output ""
        Write-Output ("=" * 72)
        Write-Output ("  {0}  --  {1}" -f $p.Name, $p.Frage)
        Write-Output ("=" * 72)
        if ($LASTEXITCODE -ne 0) {
            $log
            Write-Output "  UEBERSETZEN FEHLGESCHLAGEN"
            $fehler++
            continue
        }
        & "$bau\$($p.Name).exe" @($p.Args)
        if ($LASTEXITCODE -ne 0) {
            Write-Output ("  -> Rueckgabewert {0}" -f $LASTEXITCODE)
            $fehler++
        }
    }
}
finally {
    Set-Location $vorher
    Remove-Item $bau -Recurse -Force
}

Write-Output ""
if ($fehler -eq 0) { Write-Output "Alle Pruefungen durchgelaufen." }
else { Write-Output "$fehler Pruefung(en) mit Beanstandung." }
exit $fehler
