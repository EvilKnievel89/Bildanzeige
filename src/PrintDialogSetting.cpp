#include "PrintDialogSetting.h"

namespace
{
    // Ein und derselbe Pfad unter beiden Wurzeln; welche gilt, sagt der Zweig.
    constexpr wchar_t kKeyPath[] = L"Software\\Microsoft\\Print\\UnifiedPrintDialog";
    constexpr wchar_t kValueName[] = L"PreferLegacyPrintDialog";

    HKEY RootFor(RegistryScope scope)
    {
        return scope == RegistryScope::LocalMachine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    }

    // Liefert false, wenn der Wert fehlt oder von anderem Typ ist. Beides ist
    // kein Fehler: der Auslieferungszustand ist, dass er gar nicht dasteht.
    bool ReadValue(RegistryScope scope, DWORD& value)
    {
        DWORD data = 0;
        DWORD size = sizeof(data);
        const LSTATUS status = RegGetValueW(RootFor(scope), kKeyPath, kValueName, RRF_RT_REG_DWORD,
                                            nullptr, &data, &size);
        if (status != ERROR_SUCCESS)
            return false;

        value = data;
        return true;
    }

    std::wstring WriteError(RegistryScope scope, LSTATUS status)
    {
        std::wstring message = L"Der Wert \"";
        message += ScopeKeyName(scope);
        message += L"\\";
        message += kKeyPath;
        message += L"\\";
        message += kValueName;
        message += L"\" ließ sich nicht schreiben.\n\n";
        message += FormatHResult(HRESULT_FROM_WIN32(static_cast<DWORD>(status)));

        if (status == ERROR_ACCESS_DENIED && scope == RegistryScope::LocalMachine)
            message += L"\n\nFür alle Benutzer zu schreiben verlangt erhöhte Rechte.";
        return message;
    }
}

RegistryScope PrintDialogScope()
{
    if (ProcessScope() == RegistryScope::LocalMachine &&
        HasLegacyPrintDialog(RegistryScope::LocalMachine))
    {
        return RegistryScope::LocalMachine;
    }
    return RegistryScope::CurrentUser;
}

bool HasLegacyPrintDialog(RegistryScope scope)
{
    DWORD value = 0;
    return ReadValue(scope, value);
}

bool IsLegacyPrintDialogEnabled(RegistryScope scope)
{
    DWORD value = 0;
    return ReadValue(scope, value) && value != 0;
}

bool SetLegacyPrintDialog(RegistryScope scope, bool enabled, std::wstring& error)
{
    HKEY key = nullptr;
    LSTATUS status = ERROR_SUCCESS;
    if (enabled)
    {
        status = RegCreateKeyExW(RootFor(scope), kKeyPath, 0, nullptr, REG_OPTION_NON_VOLATILE,
                                 KEY_SET_VALUE, nullptr, &key, nullptr);
    }
    else
    {
        // Zum Abschalten wird der Schlüssel nur geöffnet, nicht angelegt. Ist
        // er nicht da, ist auch nichts abzuschalten -- ihn eigens dafür zu
        // erzeugen hinterließe eine leere Hülle für nichts.
        status = RegOpenKeyExW(RootFor(scope), kKeyPath, 0, KEY_SET_VALUE, &key);
        if (status == ERROR_FILE_NOT_FOUND)
            return true;
    }
    if (status != ERROR_SUCCESS)
    {
        error = WriteError(scope, status);
        return false;
    }

    if (enabled)
    {
        const DWORD value = 1;
        status = RegSetValueExW(key, kValueName, 0, REG_DWORD,
                                reinterpret_cast<const BYTE*>(&value), sizeof(value));
    }
    else if (scope == RegistryScope::LocalMachine)
    {
        // Im Rechnerzweig bleibt der Wert stehen und wird auf 0 gesetzt, statt
        // ihn zu entfernen. Er ist dort die Bedingung dafür, dass dieses
        // Fenster überhaupt in den Rechnerzweig schreibt (PrintDialogScope):
        // gelöscht, wäre er beim nächsten Öffnen fort, und die Einstellung
        // spränge lautlos in den Zweig des Benutzers.
        const DWORD value = 0;
        status = RegSetValueExW(key, kValueName, 0, REG_DWORD,
                                reinterpret_cast<const BYTE*>(&value), sizeof(value));
    }
    else
    {
        // Beim Benutzer dagegen wird er entfernt: 0 und "nicht vorhanden"
        // wirken gleich, und was die Anwendung nicht mehr braucht, soll sie
        // auch nicht in der Registrierung liegen lassen. Fehlt er schon, ist
        // das kein Fehlschlag -- der gewünschte Zustand steht ja da.
        status = RegDeleteValueW(key, kValueName);
        if (status == ERROR_FILE_NOT_FOUND)
            status = ERROR_SUCCESS;
    }

    RegCloseKey(key);

    if (status != ERROR_SUCCESS)
    {
        error = WriteError(scope, status);
        return false;
    }
    return true;
}
