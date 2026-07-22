#include "ImageDocument.h"

#include <shlwapi.h>

#include <cwctype>

namespace
{
    // Windows meldet Decoder an, deren Umsetzung es nicht mitliefert: WebP,
    // HEIF und AVIF stecken in Erweiterungen aus dem Microsoft Store, die sich
    // unter der CLSID einhaengen, die windowscodecs.dll bereits fuehrt. Fehlt
    // die Erweiterung, bleibt der Eintrag also stehen, und erst das Erzeugen
    // scheitert -- daher WINCODEC_ERR_COMPONENTINITIALIZEFAILURE und nicht
    // "Format unbekannt". Auf Windows Server gibt es keinen Store; dort fehlen
    // sie im Auslieferungszustand allesamt.
    //
    // Der blosse Fehlercode fuehrt in die Irre, weil er nach einem Fehler in
    // der Datei klingt. Der Hinweis nennt deshalb, was zu installieren ist.
    struct StoreCodec
    {
        const wchar_t* extensions;   // klein geschrieben, jede in Semikola
        const wchar_t* missing;      // schliesst an "es " an
    };

    const StoreCodec kStoreCodecs[] = {
        { L";.webp;",
          L"fehlt die Webp-Bildererweiterung aus dem Microsoft Store "
          L"(Kennung 9PG2DK419DRG)" },
        { L";.heic;.heif;.hif;.avci;.heics;.heifs;.avcs;",
          L"fehlt die HEIF-Bilderweiterung aus dem Microsoft Store "
          L"(Kennung 9PMMSR1CGPWG)" },
        { L";.avif;.avifs;",
          L"fehlen die HEIF-Bilderweiterung (9PMMSR1CGPWG) und die "
          L"AV1-Videoerweiterung (9MVZQVXJBQ9V) aus dem Microsoft Store" },
        { L";.jxl;",
          L"fehlt die JPEG XL-Bilderweiterung aus dem Microsoft Store "
          L"(Kennung 9MZPRTH5C0TB)" },
    };

    std::wstring MissingCodecHint(const std::wstring& path)
    {
        std::wstring extension = PathFindExtensionW(path.c_str());
        for (wchar_t& c : extension)
            c = static_cast<wchar_t>(std::towlower(c));

        std::wstring hint = L"Der Decoder fuer ";
        hint += extension.empty() ? L"dieses Format" : (L"\"" + extension + L"\"");
        hint += L" ist angemeldet, laesst sich auf diesem Rechner aber nicht erzeugen";

        const std::wstring needle = L";" + extension + L";";
        for (const StoreCodec& codec : kStoreCodecs)
        {
            if (!extension.empty() && wcsstr(codec.extensions, needle.c_str()) != nullptr)
            {
                hint += L": es ";
                hint += codec.missing;
                hint += L". Auf Windows Server sind diese Erweiterungen im "
                        L"Auslieferungszustand nicht vorhanden.";
                return hint;
            }
        }

        hint += L". Meist fehlt dazu eine Bilderweiterung aus dem Microsoft Store; "
                L"auf Windows Server sind diese im Auslieferungszustand nicht vorhanden.";
        return hint;
    }

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
        error = L"Datei konnte nicht geoeffnet werden.\n\n";

        // Nur bei diesem einen Code steht fest, dass der Decoder eingetragen
        // ist und allein seine Umsetzung fehlt. WINCODEC_ERR_COMPONENTNOTFOUND
        // sieht aehnlich aus, heisst aber meist, dass die Datei gar kein Bild
        // ist -- dort waere der Hinweis schlicht falsch.
        if (hr == WINCODEC_ERR_COMPONENTINITIALIZEFAILURE)
            error += MissingCodecHint(path) + L"\n\n";

        error += FormatHResult(hr);
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
