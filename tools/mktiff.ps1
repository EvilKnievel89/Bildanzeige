# Die beiden TIFFs:
#
#   mehrseitig.tif  3 Seiten zu 900x600, 400x900 und 1400x500 -- die
#                   Einpassung muss je Seite neu rechnen
#   fax.tif         2 Seiten, 1 bpp CCITT G4 -- ohne Formatwandlung unsichtbar
#
# Geschrieben wird in den mitgegebenen Ordner, standardmaessig testdata neben
# diesem Verzeichnis. GDI+ statt WIC, weil dessen TIFF-Encoder das Anhaengen
# weiterer Seiten (SaveAdd) ohne Umstaende beherrscht.

param(
    [string]$Ziel = (Join-Path (Split-Path $PSScriptRoot -Parent) "testdata")
)

Add-Type -AssemblyName System.Drawing
$out = $Ziel
if (-not (Test-Path $out)) { New-Item -ItemType Directory -Path $out | Out-Null }

$codec = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() |
         Where-Object { $_.MimeType -eq 'image/tiff' }

function New-EP([int]$saveFlag, $compression) {
    if ($null -ne $compression) {
        $ep = New-Object System.Drawing.Imaging.EncoderParameters 2
        $ep.Param[1] = New-Object System.Drawing.Imaging.EncoderParameter(
            [System.Drawing.Imaging.Encoder]::Compression, [int]$compression)
    } else {
        $ep = New-Object System.Drawing.Imaging.EncoderParameters 1
    }
    $ep.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter(
        [System.Drawing.Imaging.Encoder]::SaveFlag, [int]$saveFlag)
    return $ep
}

function New-Page([int]$w, [int]$h, [string]$text, $back, $fore) {
    $b = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($b)
    $g.Clear($back)
    $pen = New-Object System.Drawing.Pen $fore, 6
    $g.DrawRectangle($pen, 15, 15, $w - 30, $h - 30)
    $size = [int]($w / 12)
    $f = New-Object System.Drawing.Font "Segoe UI", $size, ([System.Drawing.FontStyle]::Bold)
    $br = New-Object System.Drawing.SolidBrush $fore
    $sf = New-Object System.Drawing.StringFormat
    $sf.Alignment = 'Center'; $sf.LineAlignment = 'Center'
    $g.DrawString($text, $f, $br, (New-Object System.Drawing.RectangleF 0, 0, $w, $h), $sf)
    $g.Dispose()
    return $b
}

$MULTI = [System.Drawing.Imaging.EncoderValue]::MultiFrame
$PAGE  = [System.Drawing.Imaging.EncoderValue]::FrameDimensionPage
$FLUSH = [System.Drawing.Imaging.EncoderValue]::Flush
$CCITT4 = [System.Drawing.Imaging.EncoderValue]::CompressionCCITT4

# --- 1) Mehrseitiges Farb-TIFF mit UNTERSCHIEDLICHEN Seitengroessen ---------
$specs = @(
    @{ w = 900; h = 600; t = "Seite 1`n900 x 600" },
    @{ w = 400; h = 900; t = "Seite 2`n400 x 900" },
    @{ w = 1400; h = 500; t = "Seite 3`n1400 x 500" }
)
$pages = @()
foreach ($s in $specs) {
    $pages += New-Page $s.w $s.h $s.t ([System.Drawing.Color]::FromArgb(30, 40, 70)) ([System.Drawing.Color]::Gold)
}
$path = "$out\mehrseitig.tif"
$pages[0].Save($path, $codec, (New-EP $MULTI $null))
for ($i = 1; $i -lt $pages.Count; $i++) { $pages[0].SaveAdd($pages[$i], (New-EP $PAGE $null)) }
$pages[0].SaveAdd((New-EP $FLUSH $null))
$pages | ForEach-Object { $_.Dispose() }
Write-Output "mehrseitig.tif  -> 3 Seiten, verschiedene Groessen"

# --- 2) Fax-TIFF: 1 bpp, CCITT Group 4, 2 Seiten ---------------------------
$faxPages = @()
foreach ($n in 1, 2) {
    $c = New-Page 1728 1100 "FAX`nSeite $n von 2" ([System.Drawing.Color]::White) ([System.Drawing.Color]::Black)
    $rect = New-Object System.Drawing.Rectangle 0, 0, 1728, 1100
    $mono = $c.Clone($rect, [System.Drawing.Imaging.PixelFormat]::Format1bppIndexed)
    $c.Dispose()
    $faxPages += $mono
}
$faxPath = "$out\fax.tif"
$faxPages[0].Save($faxPath, $codec, (New-EP $MULTI $CCITT4))
$faxPages[0].SaveAdd($faxPages[1], (New-EP $PAGE $CCITT4))
$faxPages[0].SaveAdd((New-EP $FLUSH $null))
$faxPages | ForEach-Object { $_.Dispose() }
Write-Output "fax.tif         -> 2 Seiten, 1 bpp CCITT G4"
