#include "FolderNavigator.h"

#include <shlwapi.h>

#include <algorithm>
#include <cwctype>

namespace
{
    // Notnagel, falls sich die Decoder nicht aufzaehlen lassen. Deckt die
    // Formate ab, die auf jeder Windows-Installation vorhanden sind.
    const wchar_t* const kFallbackExtensions[] = {
        L".bmp", L".dib", L".gif", L".ico", L".jfif", L".jpe", L".jpeg",
        L".jpg", L".png", L".tif", L".tiff",
    };

    std::wstring ToLower(std::wstring text)
    {
        for (wchar_t& c : text)
            c = static_cast<wchar_t>(std::towlower(c));
        return text;
    }

    std::wstring Trim(const std::wstring& text)
    {
        size_t first = 0;
        size_t last = text.size();
        while (first < last && std::iswspace(text[first]))
            ++first;
        while (last > first && std::iswspace(text[last - 1]))
            --last;
        return text.substr(first, last - first);
    }

    // StrCmpLogicalW stellt "Bild2" vor "Bild10"; die rein lexikografische
    // Reihenfolge waere hier ein sichtbarer Makel. Bei Gleichstand entscheidet
    // ein gewoehnlicher Vergleich, damit die Ordnung eindeutig bleibt.
    bool NaturalLess(const std::wstring& a, const std::wstring& b)
    {
        const int order = StrCmpLogicalW(a.c_str(), b.c_str());
        return order != 0 ? order < 0 : a < b;
    }

    // Dateinamen unterscheidet Windows nicht nach Gross- und Kleinschreibung.
    bool SameName(const std::wstring& a, const std::wstring& b)
    {
        return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()),
                                    b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
    }

    std::wstring WithBackslash(const std::wstring& folder)
    {
        if (folder.empty() || folder.back() == L'\\' || folder.back() == L'/')
            return folder;
        return folder + L'\\';
    }
}

void FolderNavigator::Clear()
{
    files_.clear();
    folder_.clear();
    index_ = 0;
    scanned_ = false;
}

void FolderNavigator::Track(IWICImagingFactory* factory, const std::wstring& path)
{
    const wchar_t* file = PathFindFileNameW(path.c_str());
    if (file == nullptr || *file == L'\0')
    {
        Clear();
        return;
    }

    const std::wstring name = file;
    const std::wstring folder = path.substr(0, static_cast<size_t>(file - path.c_str()));

    if (!scanned_ || !SameName(folder, folder_))
        Scan(factory, folder);

    // Die geoeffnete Datei gehoert in jedem Fall in die Liste -- auch dann,
    // wenn kein Decoder ihre Endung beansprucht. Oeffnen liess sie sich ja.
    auto it = std::find_if(files_.begin(), files_.end(),
                           [&](const std::wstring& entry) { return SameName(entry, name); });
    if (it == files_.end())
    {
        it = files_.insert(std::lower_bound(files_.begin(), files_.end(), name, NaturalLess),
                           name);
    }
    index_ = static_cast<size_t>(it - files_.begin());
}

std::wstring FolderNavigator::FirstIn(IWICImagingFactory* factory, const std::wstring& folder)
{
    Scan(factory, WithBackslash(folder));
    if (files_.empty())
        return {};
    return folder_ + files_.front();
}

void FolderNavigator::Scan(IWICImagingFactory* factory, const std::wstring& folder)
{
    EnsureExtensions(factory);

    files_.clear();
    folder_ = folder;
    index_ = 0;
    scanned_ = true;

    WIN32_FIND_DATAW found{};
    const HANDLE handle = FindFirstFileExW((folder_ + L'*').c_str(), FindExInfoBasic, &found,
                                           FindExSearchNameMatch, nullptr, 0);
    if (handle == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            continue;
        if (Accepts(found.cFileName))
            files_.emplace_back(found.cFileName);
    } while (FindNextFileW(handle, &found));
    FindClose(handle);

    std::sort(files_.begin(), files_.end(), NaturalLess);
}

void FolderNavigator::EnsureExtensions(IWICImagingFactory* factory)
{
    if (!extensions_.empty())
        return;

    // Die Endungen kommen von den tatsaechlich registrierten Decodern, nicht
    // aus einer festen Liste im Code: ist etwa HEIF oder ein Raw-Format
    // nachinstalliert, steht es damit von selbst in der Ordnerliste.
    ComPtr<IEnumUnknown> enumerator;
    if (factory != nullptr &&
        SUCCEEDED(factory->CreateComponentEnumerator(WICDecoder, WICComponentEnumerateDefault,
                                                     enumerator.GetAddressOf())))
    {
        ComPtr<IUnknown> item;
        ULONG fetched = 0;
        while (enumerator->Next(1, item.ReleaseAndGetAddressOf(), &fetched) == S_OK && fetched == 1)
        {
            ComPtr<IWICBitmapCodecInfo> info;
            if (FAILED(item.As(&info)))
                continue;

            UINT length = 0;
            if (FAILED(info->GetFileExtensions(0, nullptr, &length)) || length <= 1)
                continue;

            std::wstring list(length, L'\0');
            if (FAILED(info->GetFileExtensions(length, list.data(), &length)))
                continue;

            // Die gemeldete Laenge schliesst den Abschluss mit ein.
            list.resize(length > 0 ? length - 1 : 0);
            AddExtensions(list);
        }
    }

    if (extensions_.empty())
    {
        for (const wchar_t* fallback : kFallbackExtensions)
            extensions_.emplace_back(fallback);
    }
}

void FolderNavigator::AddExtensions(const std::wstring& list)
{
    size_t start = 0;
    while (start <= list.size())
    {
        const size_t comma = list.find(L',', start);
        const size_t count = (comma == std::wstring::npos) ? std::wstring::npos : comma - start;
        const std::wstring item = ToLower(Trim(list.substr(start, count)));

        if (item.size() > 1 && item.front() == L'.' &&
            std::find(extensions_.begin(), extensions_.end(), item) == extensions_.end())
        {
            extensions_.push_back(item);
        }

        if (comma == std::wstring::npos)
            break;
        start = comma + 1;
    }
}

bool FolderNavigator::Accepts(const wchar_t* name) const
{
    const wchar_t* dot = PathFindExtensionW(name);
    if (dot == nullptr || *dot == L'\0')
        return false;

    const std::wstring extension = ToLower(dot);
    return std::find(extensions_.begin(), extensions_.end(), extension) != extensions_.end();
}

bool FolderNavigator::HasNeighbour(int delta) const
{
    if (files_.empty())
        return false;

    const long long target = static_cast<long long>(index_) + delta;
    return target >= 0 && target < static_cast<long long>(files_.size());
}

std::wstring FolderNavigator::Neighbour(int delta) const
{
    if (!HasNeighbour(delta))
        return {};
    return folder_ + files_[static_cast<size_t>(static_cast<long long>(index_) + delta)];
}

void FolderNavigator::Drop(const std::wstring& path)
{
    const wchar_t* file = PathFindFileNameW(path.c_str());
    if (file == nullptr)
        return;

    const std::wstring name = file;
    const auto it = std::find_if(files_.begin(), files_.end(),
                                 [&](const std::wstring& entry) { return SameName(entry, name); });
    if (it == files_.end())
        return;

    // Faellt ein Eintrag vor der aktuellen Stelle weg, rueckt diese mit nach
    // vorn -- sonst zeigte der Index nach dem Loeschen auf den Nachbarn.
    const size_t position = static_cast<size_t>(it - files_.begin());
    files_.erase(it);
    if (position < index_ && index_ > 0)
        --index_;
}
