#pragma once

#include "Common.h"

#include <wincodec.h>

#include <string>

// Eine geöffnete Bilddatei. Der Decoder bleibt offen, Frames werden einzeln
// nachgeladen -- bei mehrseitigen TIFFs wäre es Verschwendung, alles zu halten.
class ImageDocument
{
public:
    bool Open(IWICImagingFactory* factory, const std::wstring& path, std::wstring& error);
    void Close();

    bool IsOpen() const { return decoder_ != nullptr; }
    UINT FrameCount() const { return frameCount_; }
    const std::wstring& Path() const { return path_; }

    // Startdrehung in Vierteln aus der EXIF-Orientierung (Tag 274), damit ein
    // hochkant gehaltenes Foto auch hochkant erscheint. Gelesen wird sie an
    // Frame 0 und gilt für die ganze Datei: eine Drehung von Hand soll den
    // Seitenwechsel überdauern, und das ginge nicht, wenn jede Seite ihre
    // eigene Startdrehung durchsetzte.
    int OrientationQuarters() const { return orientation_; }

    // Für die Metadaten, die nur am Container hängen -- etwa die logische
    // Leinwand und die Wiederholungen eines GIFs.
    IWICBitmapDecoder* Decoder() const { return decoder_.Get(); }

    // Liefert den Frame nach 32bppPBGRA konvertiert -- das Format, das Direct2D
    // erwartet. Ohne die Konvertierung bleiben etwa 1-bpp-Fax-TIFFs unsichtbar.
    ComPtr<IWICBitmapSource> LoadFrame(IWICImagingFactory* factory, UINT index,
                                       std::wstring& error) const;

private:
    static int ReadOrientation(IWICBitmapDecoder* decoder);

    ComPtr<IWICBitmapDecoder> decoder_;
    UINT frameCount_ = 0;
    int orientation_ = 0;
    std::wstring path_;
};
