# Die beiden Grundbilder und die Notiz:
#
#   gross.png              3000x2000 -- muss verkleinert eingepasst werden
#   klein.png              120x80    -- darf *nicht* aufgeblasen werden
#   ordner\notiz.txt       keine Bilddatei, darf nicht in der Ordnerliste stehen
#
# Beide tragen ihre Masse als Aufschrift: auf einem Bildschirmabzug ist damit
# ohne Nachschlagen zu sehen, welche Datei gerade zu sehen ist. Der weisse
# Rahmen in gross.png sitzt 30 Punkte innen -- wird das Bild eingepasst, muss
# er ringsum sichtbar bleiben; ist auch nur eine Kante angeschnitten, stimmt
# die Rechnung nicht.

param(
    [string]$Ziel = (Join-Path (Split-Path $PSScriptRoot -Parent) "testdata")
)

Add-Type -AssemblyName System.Drawing

if (-not (Test-Path $Ziel)) { New-Item -ItemType Directory -Path $Ziel | Out-Null }
$ordner = Join-Path $Ziel "ordner"
if (-not (Test-Path $ordner)) { New-Item -ItemType Directory -Path $ordner | Out-Null }

function Schreibe([string]$name, [int]$w, [int]$h, [scriptblock]$malen) {
    $bmp = New-Object System.Drawing.Bitmap $w, $h,
                      ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAlias
    & $malen $g
    $g.Dispose()
    $pfad = Join-Path $Ziel $name
    $bmp.Save($pfad, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    "{0,-16} {1,5} x {2,-5} {3,9:N0} Bytes" -f $name, $w, $h, (Get-Item $pfad).Length
}

Schreibe "gross.png" 3000 2000 {
    param($g)
    # Verlauf ueber die Diagonale: an jeder Stelle eine andere Farbe, damit beim
    # Verkleinern auffaellt, wenn Zeilen oder Spalten uebersprungen werden.
    $verlauf = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.Point 0,0),
        (New-Object System.Drawing.Point 3000,2000),
        [System.Drawing.Color]::DarkSlateBlue,
        [System.Drawing.Color]::Orange)
    $g.FillRectangle($verlauf, 0, 0, 3000, 2000)
    $verlauf.Dispose()

    $stift = New-Object System.Drawing.Pen ([System.Drawing.Color]::White), 8
    $g.DrawRectangle($stift, 30, 30, 3000-60, 2000-60)
    $stift.Dispose()

    $schrift = New-Object System.Drawing.Font "Segoe UI", 200, ([System.Drawing.FontStyle]::Bold),
                          ([System.Drawing.GraphicsUnit]::Pixel)
    $g.DrawString("3000 x 2000", $schrift, [System.Drawing.Brushes]::White, 240, 880)
    $schrift.Dispose()
}

Schreibe "klein.png" 120 80 {
    param($g)
    $g.Clear([System.Drawing.Color]::Crimson)
    $schrift = New-Object System.Drawing.Font "Segoe UI", 16, ([System.Drawing.FontStyle]::Regular),
                          ([System.Drawing.GraphicsUnit]::Pixel)
    $g.DrawString("klein", $schrift, [System.Drawing.Brushes]::White, 10, 24)
    $schrift.Dispose()
}

# Bewusst ueber WriteAllText: Set-Content haengt einen Zeilenumbruch aus zwei
# Zeichen an und schriebe je nach Fassung von PowerShell eine Stueckliste
# voran. Hier steht genau, was in der Datei landet.
$notiz = Join-Path $ordner "notiz.txt"
[System.IO.File]::WriteAllText($notiz, "Kein Bild. Darf in der Ordnerliste nicht auftauchen.`n",
                               (New-Object System.Text.UTF8Encoding $false))
"{0,-16} {1,29:N0} Bytes" -f "ordner\notiz.txt", (Get-Item $notiz).Length
