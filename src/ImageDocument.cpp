#include "ImageDocument.h"

namespace
{
    // EXIF-Tag 274. Wo die IFD haengt, entscheidet der Container: JPEG und HEIF
    // legen sie in den APP1-Block, TIFF hat sie unmittelbar. Beide Pfade werden
    // versucht, statt den Containertyp abzufragen -- ein fehlender Pfad kostet
    // nur einen fehlgeschlagenen Aufruf.
    const wchar_t* const kOrientationPaths[] = {
        L"/app1/ifd/{ushort=274}",
        L"/ifd/{ushort=274}",
    };

    bool ReadUInt(const PROPVARIANT& value, unsigned& out)
    {
        switch (value.vt)
        {
        case VT_UI1: out = value.bVal;    return true;
        case VT_UI2: out = value.uiVal;   return true;
        case VT_UI4: out = value.ulVal;   return true;
        case VT_I2:  out = static_cast<unsigned>(value.iVal);  return true;
        case VT_I4:  out = static_cast<unsigned>(value.lVal);  return true;
        default:     return false;
        }
    }

    // EXIF 1..8 auf Vierteldrehungen im Uhrzeigersinn.
    //
    // Die vier gespiegelten Faelle (2, 4, 5, 7) entstehen aus Kameras praktisch
    // nie -- sie stammen aus Bildbearbeitung, die das Spiegeln in die Metadaten
    // geschrieben hat. Von ihnen wird nur der Drehanteil uebernommen; eine
    // Spiegelung kennt die Anzeige nicht, und sie dafuer einzufuehren hiesse,
    // Drehung und Spiegelung durch die ganze Ansicht zu ziehen.
    int QuartersFromOrientation(unsigned value)
    {
        switch (value)
        {
        case 3: case 4: return 2;   // 180 Grad
        case 5: case 6: return 1;   // 90 Grad im Uhrzeigersinn
        case 7: case 8: return 3;   // 270 Grad im Uhrzeigersinn
        default:        return 0;
        }
    }
}

bool ImageDocument::Open(IWICImagingFactory* factory, const std::wstring& path, std::wstring& error)
{
    Close();

    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = factory->CreateDecoderFromFilename(
        path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr))
    {
        error = L"Datei konnte nicht geoeffnet werden.\n\n" + FormatHResult(hr);
        return false;
    }

    UINT frameCount = 0;
    hr = decoder->GetFrameCount(&frameCount);
    if (FAILED(hr) || frameCount == 0)
    {
        error = L"Die Datei enthaelt kein lesbares Bild.\n\n" + FormatHResult(hr);
        return false;
    }

    decoder_ = decoder;
    frameCount_ = frameCount;
    orientation_ = ReadOrientation(decoder_.Get());
    path_ = path;
    return true;
}

void ImageDocument::Close()
{
    decoder_.Reset();
    frameCount_ = 0;
    orientation_ = 0;
    path_.clear();
}

int ImageDocument::ReadOrientation(IWICBitmapDecoder* decoder)
{
    ComPtr<IWICBitmapFrameDecode> frame;
    if (decoder == nullptr || FAILED(decoder->GetFrame(0, &frame)))
        return 0;

    // Die meisten Dateien haben ueberhaupt keine Metadaten; das schlaegt hier
    // fehl und ist kein Fehler, sondern der Normalfall.
    ComPtr<IWICMetadataQueryReader> reader;
    if (FAILED(frame->GetMetadataQueryReader(&reader)))
        return 0;

    for (const wchar_t* path : kOrientationPaths)
    {
        PROPVARIANT value;
        PropVariantInit(&value);

        unsigned raw = 0;
        const bool read = SUCCEEDED(reader->GetMetadataByName(path, &value)) &&
                          ReadUInt(value, raw);
        PropVariantClear(&value);

        // Ausserhalb von 1..8 ist der Wert kaputt; dann lieber ungedreht
        // zeigen, als das Bild auf gut Glueck zu kippen.
        if (read && raw >= 1 && raw <= 8)
            return QuartersFromOrientation(raw);
    }
    return 0;
}

ComPtr<IWICBitmapSource> ImageDocument::LoadFrame(IWICImagingFactory* factory, UINT index,
                                                  std::wstring& error) const
{
    if (!decoder_ || index >= frameCount_)
    {
        error = L"Ungueltiger Frame-Index.";
        return nullptr;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    HRESULT hr = decoder_->GetFrame(index, &frame);
    if (FAILED(hr))
    {
        error = L"Frame konnte nicht gelesen werden.\n\n" + FormatHResult(hr);
        return nullptr;
    }

    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr))
    {
        error = L"Formatkonverter nicht verfuegbar.\n\n" + FormatHResult(hr);
        return nullptr;
    }

    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                               WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr))
    {
        error = L"Bildformat wird nicht unterstuetzt.\n\n" + FormatHResult(hr);
        return nullptr;
    }

    ComPtr<IWICBitmapSource> source;
    hr = converter.As(&source);
    if (FAILED(hr))
    {
        error = FormatHResult(hr);
        return nullptr;
    }
    return source;
}
