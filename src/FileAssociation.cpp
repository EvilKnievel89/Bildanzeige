#include "FileAssociation.h"

#include <shellapi.h>
#include <shlobj.h>     // SHChangeNotify
#include <shlwapi.h>
#include <wincodec.h>

#include <algorithm>
#include <cwctype>
#include <utility>

// Was hier geschrieben wird und was mit Absicht nicht:
//
//     Software\Classes\Bildanzeige.jpg          ProgId je Endung
//         (Vorgabe), FriendlyTypeName           Name in der Spalte "Typ"
//         DefaultIcon                           "<EXE>",0
//         shell\open\command                     "<EXE>" "%1"
//     Software\Classes\.jpg\OpenWithProgids
//         Bildanzeige.jpg                       leerer Wert -- reiht sich in
//                                               "Öffnen mit" ein
//     Software\Classes\Applications\Bildanzeige.exe
//         FriendlyAppName, DefaultIcon, shell\open\command, SupportedTypes
//     Software\Bildanzeige\Capabilities
//         ApplicationName, ApplicationDescription, ApplicationIcon
//         FileAssociations\.jpg = Bildanzeige.jpg
//     Software\RegisteredApplications
//         Bildanzeige = Software\Bildanzeige\Capabilities
//
// Der Vorgabewert von "Software\Classes\.jpg" bleibt unangetastet. Er ist die
// Zuordnung selbst, und sie gehört dem Benutzer: seit Windows 8 entscheidet
// ohnehin "UserChoice" unter FileExts, und dieser Wert trägt eine Prüfsumme,
// die nur Windows selbst bilden kann. Ein Programm, das die Zuordnung an sich
// reißt, erreicht damit heute nichts weiter, als den Explorer beim nächsten
// Doppelklick nachfragen zu lassen. Eingetragen wird also die Fähigkeit, nicht
// die Wahl -- und für die Wahl führt ShowDefaultAppsUI dorthin, wo sie
// getroffen wird.

namespace
{
    constexpr wchar_t kAppName[] = L"Bildanzeige";
    constexpr wchar_t kAppDescription[] =
        L"Schlanker Bildbetrachter im Geist der Windows Bild- und Faxanzeige.";

    // Unterhalb von Software\Classes; der Zweig darüber entscheidet, ob es der
    // Benutzer oder der Rechner ist.
    const std::wstring kClasses = L"Software\\Classes\\";
    const std::wstring kAppRoot = L"Software\\Bildanzeige";
    const std::wstring kCapabilities = kAppRoot + L"\\Capabilities";
    const std::wstring kRegisteredApps = L"Software\\RegisteredApplications";

    HKEY RootFor(RegistryScope scope)
    {
        return scope == RegistryScope::LocalMachine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    }

    // Je Endung eine eigene ProgId, nicht eine gemeinsame für alle. Der
    // Explorer nimmt aus ihr die Spalte "Typ": mit einer einzigen hieße jede
    // Bilddatei gleich, und aus "JPEG-Bild" und "PNG-Bild" würde zweimal
    // "Bild". Der Punkt kommt aus der Endung mit -- "Bildanzeige" + ".jpg".
    std::wstring ProgIdFor(const std::wstring& extension)
    {
        return kAppName + extension;
    }

    // Der Zweig unter Applications heißt nach der EXE, nicht nach der
    // Anwendung: wer sie umbenennt, trägt sie unter dem neuen Namen ein.
    //
    // Ohne eigenen Pfad bleibt der Rückgabewert leer, und jeder Aufrufer hier
    // prüft darauf. Der Grund steht in RemoveApplication.
    std::wstring ApplicationsKey()
    {
        const std::wstring& exe = ExecutablePath();
        if (exe.empty())
            return std::wstring();
        return kClasses + L"Applications\\" + PathFindFileNameW(exe.c_str());
    }

    class RegKey
    {
    public:
        RegKey() = default;
        RegKey(const RegKey&) = delete;
        RegKey& operator=(const RegKey&) = delete;
        ~RegKey()
        {
            if (key_ != nullptr)
                RegCloseKey(key_);
        }

        LSTATUS Create(HKEY root, const std::wstring& path)
        {
            return RegCreateKeyExW(root, path.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                                   KEY_READ | KEY_WRITE, nullptr, &key_, nullptr);
        }

        LSTATUS Open(HKEY root, const std::wstring& path, REGSAM access)
        {
            return RegOpenKeyExW(root, path.c_str(), 0, access, &key_);
        }

        HKEY Get() const { return key_; }

    private:
        HKEY key_ = nullptr;
    };

    // name == nullptr schreibt den Vorgabewert des Schlüssels.
    LSTATUS WriteValue(HKEY root, const std::wstring& path, const wchar_t* name,
                       const std::wstring& value)
    {
        RegKey key;
        const LSTATUS status = key.Create(root, path);
        if (status != ERROR_SUCCESS)
            return status;

        return RegSetValueExW(key.Get(), name, 0, REG_SZ,
                              reinterpret_cast<const BYTE*>(value.c_str()),
                              static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    }

    bool KeyExists(HKEY root, const std::wstring& path)
    {
        RegKey key;
        return key.Open(root, path, KEY_QUERY_VALUE) == ERROR_SUCCESS;
    }

    void DeleteValue(HKEY root, const std::wstring& path, const wchar_t* name)
    {
        RegKey key;
        if (key.Open(root, path, KEY_SET_VALUE) == ERROR_SUCCESS)
            RegDeleteValueW(key.Get(), name);
    }

    // Nur, wenn nichts mehr darin steht. Ein "Software\Classes\.jpg" kann von
    // jemand anderem stammen und anderes enthalten; abgeräumt wird deshalb die
    // eigene Spur und nicht der Schlüssel, in dem sie lag.
    void DeleteKeyIfEmpty(HKEY root, const std::wstring& path)
    {
        DWORD subkeys = 0;
        DWORD values = 0;
        {
            RegKey key;
            if (key.Open(root, path, KEY_READ) != ERROR_SUCCESS)
                return;
            if (RegQueryInfoKeyW(key.Get(), nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr,
                                 &values, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            {
                return;
            }
        }

        // Erst schließen, dann löschen: ein Schlüssel mit offenem Handle
        // verschwindet zwar auch, aber erst wenn das letzte davon fällt.
        if (subkeys == 0 && values == 0)
            RegDeleteKeyW(root, path.c_str());
    }

    std::wstring WriteError(RegistryScope scope, const std::wstring& path, LSTATUS status)
    {
        std::wstring message = L"Der Eintrag \"";
        message += ScopeKeyName(scope);
        message += L"\\" + path + L"\" ließ sich nicht schreiben.\n\n";
        message += FormatHResult(HRESULT_FROM_WIN32(static_cast<DWORD>(status)));

        if (status == ERROR_ACCESS_DENIED && scope == RegistryScope::LocalMachine)
            message += L"\n\nFür alle Benutzer einzutragen verlangt erhöhte Rechte.";
        return message;
    }

    struct Entry
    {
        std::wstring path;
        const wchar_t* name;    // nullptr == Vorgabewert
        std::wstring value;
    };

    bool WriteEntries(HKEY root, RegistryScope scope, const std::vector<Entry>& entries,
                      std::wstring& error)
    {
        for (const Entry& entry : entries)
        {
            const LSTATUS status = WriteValue(root, entry.path, entry.name, entry.value);
            if (status != ERROR_SUCCESS)
            {
                error = WriteError(scope, entry.path, status);
                return false;
            }
        }
        return true;
    }

    std::wstring OpenCommand()
    {
        return L"\"" + ExecutablePath() + L"\" \"%1\"";
    }

    // Anführungszeichen um den Pfad, obwohl das Symbol meist ohne sie steht:
    // die Shell trennt die Nummer am letzten Komma ab, und ein Ordnername darf
    // ein Komma enthalten. PathParseIconLocation nimmt die Anführungszeichen
    // wieder fort.
    std::wstring IconLocation()
    {
        return L"\"" + ExecutablePath() + L"\",0";
    }

    bool WriteType(HKEY root, RegistryScope scope, const FileType& type, std::wstring& error)
    {
        const std::wstring progId = ProgIdFor(type.extension);
        const std::wstring progIdKey = kClasses + progId;

        const std::vector<Entry> entries = {
            // Der Name des Typs steht zweimal: als Vorgabewert, den ältere
            // Wege lesen, und als FriendlyTypeName, den der Explorer vorzieht.
            { progIdKey, nullptr, type.name },
            { progIdKey, L"FriendlyTypeName", type.name },
            { progIdKey + L"\\DefaultIcon", nullptr, IconLocation() },

            // Der Vorgabewert von shell\open bleibt leer. "open" ist ein
            // bekanntes Verb; Windows setzt die übersetzte Beschriftung
            // ("Öffnen") selbst ein und trifft dabei die Sprache des Systems,
            // was ein festes Wort hier nicht könnte.
            { progIdKey + L"\\shell\\open\\command", nullptr, OpenCommand() },

            // Der Verweis von der Endung auf die ProgId. Er ändert den
            // Standard nicht -- er reiht die Anwendung in "Öffnen mit" ein.
            { kClasses + type.extension + L"\\OpenWithProgids", progId.c_str(), L"" },

            { ApplicationsKey() + L"\\SupportedTypes", type.extension, L"" },
            { kCapabilities + L"\\FileAssociations", type.extension, progId },
        };
        return WriteEntries(root, scope, entries, error);
    }

    void RemoveType(HKEY root, const FileType& type)
    {
        const std::wstring progId = ProgIdFor(type.extension);
        RegDeleteTreeW(root, (kClasses + progId).c_str());

        const std::wstring openWith = kClasses + type.extension + L"\\OpenWithProgids";
        DeleteValue(root, openWith, progId.c_str());
        DeleteKeyIfEmpty(root, openWith);
        DeleteKeyIfEmpty(root, kClasses + type.extension);

        DeleteValue(root, ApplicationsKey() + L"\\SupportedTypes", type.extension);
        DeleteValue(root, kCapabilities + L"\\FileAssociations", type.extension);
    }

    // Die Angaben zur Anwendung selbst -- sie stehen einmal da, gleichgültig
    // wie viele Endungen eingetragen sind.
    bool WriteApplication(HKEY root, RegistryScope scope, std::wstring& error)
    {
        const std::wstring application = ApplicationsKey();

        const std::vector<Entry> entries = {
            { application, L"FriendlyAppName", kAppName },
            { application + L"\\DefaultIcon", nullptr, IconLocation() },
            { application + L"\\shell\\open\\command", nullptr, OpenCommand() },

            // Capabilities ist der Zweig, aus dem die Einstellungen
            // "Standard-Apps" die Anwendung mit Namen, Beschreibung und Symbol
            // aufführen; RegisteredApplications zeigt darauf. Ohne beides
            // stünde die Bildanzeige dort nicht, und der Benutzer könnte sie
            // gar nicht zum Standard machen.
            { kCapabilities, L"ApplicationName", kAppName },
            { kCapabilities, L"ApplicationDescription", kAppDescription },
            { kCapabilities, L"ApplicationIcon", IconLocation() },
            { kRegisteredApps, kAppName, kCapabilities },
        };
        return WriteEntries(root, scope, entries, error);
    }

    void RemoveApplication(HKEY root)
    {
        // Der leere Pfad wird abgefangen, obwohl ApplyRegistration ihn längst
        // ausgeschlossen hat. RegDeleteTree löscht den genannten Schlüssel
        // samt allem darunter, und aus einem leeren Dateinamen würde
        // "Software\Classes\Applications" -- also der Eintrag jeder
        // Anwendung auf dem Rechner. Ein Fehler, den man nur einmal macht.
        const std::wstring application = ApplicationsKey();
        if (!application.empty())
            RegDeleteTreeW(root, application.c_str());

        RegDeleteTreeW(root, kAppRoot.c_str());
        DeleteValue(root, kRegisteredApps, kAppName);
    }

    // Führt dieser Decoder die Endung? Die Liste kommt durch Komma getrennt und
    // in wechselnder Schreibweise heraus -- ".JXL" steht neben ".webp".
    bool ListsExtension(IWICBitmapDecoderInfo* info, const std::wstring& extension)
    {
        UINT needed = 0;
        if (FAILED(info->GetFileExtensions(0, nullptr, &needed)) || needed == 0)
            return false;

        std::wstring list(needed, L'\0');
        if (FAILED(info->GetFileExtensions(needed, list.data(), &needed)))
            return false;

        list.resize(wcslen(list.c_str()));
        for (wchar_t& c : list)
            c = static_cast<wchar_t>(std::towlower(c));

        // Komma an beiden Enden, damit ".jpe" nicht in ".jpeg" gefunden wird.
        return (L"," + list + L",").find(L"," + extension + L",") != std::wstring::npos;
    }

    // Derselbe Weg, den tools\pruefungen\decoder.cpp geht: den Decoder einmal
    // probeweise erzeugen. Fehlt die Erweiterung aus dem Store, steht ihr
    // Eintrag trotzdem da, und erst das Erzeugen scheitert -- mit
    // WINCODEC_ERR_COMPONENTINITIALIZEFAILURE (Abschnitt 7).
    bool ProbeDecoder(const std::wstring& extension)
    {
        ComPtr<IWICImagingFactory> factory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&factory))))
        {
            return false;
        }

        ComPtr<IEnumUnknown> enumerator;
        if (FAILED(factory->CreateComponentEnumerator(WICDecoder, WICComponentEnumerateDefault,
                                                      &enumerator)))
        {
            return false;
        }

        ComPtr<IUnknown> element;
        ULONG fetched = 0;
        while (enumerator->Next(1, element.ReleaseAndGetAddressOf(), &fetched) == S_OK &&
               fetched == 1)
        {
            ComPtr<IWICBitmapDecoderInfo> info;
            if (FAILED(element.As(&info)) || !ListsExtension(info.Get(), extension))
                continue;

            ComPtr<IWICBitmapDecoder> decoder;
            return SUCCEEDED(info->CreateInstance(&decoder));
        }
        return false;
    }

    bool Launch(const std::wstring& target)
    {
        const INT_PTR result = reinterpret_cast<INT_PTR>(
            ShellExecuteW(nullptr, nullptr, target.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        return result > 32;
    }
}

const std::vector<FileType>& FileTypes()
{
    // Die elf Formate, die jede Windows-Installation von sich aus dekodiert
    // (dieselben, die FolderNavigator als Notnagel führt), und die fünf, die
    // aus einer Erweiterung des Microsoft Store kommen. Letztere sind
    // gekennzeichnet, weil ihr Decoder angemeldet sein kann, ohne vorhanden zu
    // sein -- gefragt wird deshalb vor dem Vorhaken bei CanDecode, und wo die
    // Erweiterung fehlt, sagt es die Liste im Fenster.
    //
    // .ico ist als einziges vorhandenes nicht vorgehakt: ein Symbol ist eher
    // Zubehör eines Programms als ein Bild, das man betrachtet. Wer es doch
    // will, hakt es an.
    //
    // Zu jedem Store-Format gibt es mehr Endungen, als hier stehen (.hif,
    // .avci, .heics, .avifs …). Aufgenommen sind die, die einem tatsächlich
    // begegnen; die Anwendung zeigt die übrigen weiterhin an, sie stehen nur
    // nicht zur Zuordnung.
    static const std::vector<FileType> types = {
        { L".avif", L"AVIF-Bild",    true,  true  },
        { L".bmp",  L"BMP-Bild",     true,  false },
        { L".dib",  L"DIB-Bild",     true,  false },
        { L".gif",  L"GIF-Bild",     true,  false },
        { L".heic", L"HEIC-Bild",    true,  true  },
        { L".heif", L"HEIF-Bild",    true,  true  },
        { L".ico",  L"Symboldatei",  false, false },
        { L".jfif", L"JPEG-Bild",    true,  false },
        { L".jpe",  L"JPEG-Bild",    true,  false },
        { L".jpeg", L"JPEG-Bild",    true,  false },
        { L".jpg",  L"JPEG-Bild",    true,  false },
        { L".jxl",  L"JPEG XL-Bild", true,  true  },
        { L".png",  L"PNG-Bild",     true,  false },
        { L".tif",  L"TIFF-Bild",    true,  false },
        { L".tiff", L"TIFF-Bild",    true,  false },
        { L".webp", L"WebP-Bild",    true,  true  },
    };
    return types;
}

bool CanDecode(const std::wstring& extension)
{
    // Das Ergebnis wird gemerkt: der Versuch lädt die Codec-DLL, und die
    // Anzeige fragt bei jedem Fensterwechsel neu. Dass eine Erweiterung
    // während des Laufs nachinstalliert wird, ist der seltenere Fall gegen den
    // täglichen -- und ein neuer Start zeigt sie dann.
    static std::vector<std::pair<std::wstring, bool>> known;
    for (const std::pair<std::wstring, bool>& entry : known)
    {
        if (entry.first == extension)
            return entry.second;
    }

    const bool usable = ProbeDecoder(extension);
    known.emplace_back(extension, usable);
    return usable;
}

// Gefragt ist nicht, ob der Benutzer Administrator *ist*, sondern ob dieser
// Lauf nach HKEY_LOCAL_MACHINE schreiben darf. Unter der Benutzerkontensteuerung
// bekommt auch ein Administrator ein beschnittenes Zugriffstoken, solange er
// nicht ausdrücklich erhöht startet -- und mit dem kommt er dort nicht hinein.
// TokenElevation beantwortet genau das.
RegistryScope ProcessScope()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return RegistryScope::CurrentUser;

    TOKEN_ELEVATION elevation{};
    DWORD returned = 0;
    const bool elevated =
        GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &returned) &&
        elevation.TokenIsElevated != 0;

    CloseHandle(token);
    return elevated ? RegistryScope::LocalMachine : RegistryScope::CurrentUser;
}

const wchar_t* ScopeKeyName(RegistryScope scope)
{
    return scope == RegistryScope::LocalMachine ? L"HKEY_LOCAL_MACHINE" : L"HKEY_CURRENT_USER";
}

const std::wstring& ExecutablePath()
{
    static const std::wstring path = []
    {
        std::wstring buffer(MAX_PATH, L'\0');
        for (;;)
        {
            const DWORD length =
                GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0)
                return std::wstring();

            // Ist der Puffer zu klein, füllt Windows ihn ganz aus und meldet
            // genau seine Länge zurück -- daran ist der Fall zu erkennen.
            if (length < buffer.size())
            {
                buffer.resize(length);
                return buffer;
            }
            buffer.resize(buffer.size() * 2);
        }
    }();
    return path;
}

bool IsRegistered(RegistryScope scope, const std::wstring& extension)
{
    return KeyExists(RootFor(scope),
                     kClasses + ProgIdFor(extension) + L"\\shell\\open\\command");
}

bool IsDefaultHandler(const std::wstring& extension)
{
    const std::wstring& own = ExecutablePath();
    if (own.empty())
        return false;

    // AssocQueryString beantwortet die Frage so, wie der Explorer sie stellt:
    // über UserChoice, Zweig des Benutzers, Zweig des Rechners. Verglichen wird
    // ohne Rücksicht auf Groß- und Kleinschreibung, aber sonst Zeichen für
    // Zeichen -- zwei Wege zur selben Datei (Kurzname, Verzweigung) gelten
    // hier als verschieden. Das ist die vorsichtige Seite des Irrtums: die
    // Anzeige behauptet dann zu wenig statt zu viel.
    wchar_t buffer[MAX_PATH]{};
    DWORD length = ARRAYSIZE(buffer);
    if (AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, extension.c_str(), L"open", buffer,
                          &length) != S_OK)
    {
        return false;
    }

    return CompareStringOrdinal(buffer, -1, own.c_str(), -1, TRUE) == CSTR_EQUAL;
}

bool ApplyRegistration(RegistryScope scope, const std::vector<std::wstring>& wanted,
                       std::wstring& error)
{
    if (ExecutablePath().empty())
    {
        error = L"Der eigene Pfad ließ sich nicht ermitteln.";
        return false;
    }

    const HKEY root = RootFor(scope);

    bool ok = true;
    for (const FileType& type : FileTypes())
    {
        const bool keep = std::find(wanted.begin(), wanted.end(), type.extension) != wanted.end();
        if (!keep)
        {
            RemoveType(root, type);
            continue;
        }

        // Der Pfad wird bei jedem Übernehmen neu geschrieben. Die EXE ist
        // tragbar: wer sie in einen anderen Ordner legt, rückt den Eintrag
        // damit wieder gerade, ohne ihn vorher abmelden zu müssen.
        if (!WriteType(root, scope, type, error))
        {
            ok = false;
            break;
        }
    }

    if (ok)
    {
        if (wanted.empty())
            RemoveApplication(root);
        else
            ok = WriteApplication(root, scope, error);
    }

    // Der Explorer hält die Zuordnungen im Gedächtnis. Ohne diese Nachricht
    // stünde die Änderung erst nach dem nächsten Anmelden im Kontextmenü.
    // Sie geht auch nach einem Fehlschlag hinaus: dann steht die Hälfte
    // geschrieben, und die soll ebenso sichtbar werden.
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSH, nullptr, nullptr);
    return ok;
}

bool ShowDefaultAppsUI(RegistryScope scope, std::wstring& error)
{
    // Windows 11 kennt eine Vertiefung auf die eigene Anwendung; kennt eine
    // Fassung sie nicht, öffnet sie die Übersicht. Beides führt an die Stelle,
    // an der die Wahl getroffen wird -- deshalb genügt als Rückfallweg dieselbe
    // Adresse ohne den Zusatz.
    const std::wstring app = std::wstring(L"ms-settings:defaultapps?registeredApp") +
                             (scope == RegistryScope::LocalMachine ? L"Machine=" : L"User=") +
                             kAppName;

    if (Launch(app) || Launch(L"ms-settings:defaultapps"))
        return true;

    error = L"Die Einstellungen \"Standard-Apps\" ließen sich nicht öffnen.\n\n"
            L"Die Zuordnung lässt sich auch im Explorer treffen: Rechtsklick auf eine "
            L"Bilddatei, \"Öffnen mit\", \"Andere App auswählen\".";
    return false;
}
