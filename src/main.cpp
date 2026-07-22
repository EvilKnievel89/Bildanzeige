#include "FileAssociation.h"
#include "MainWindow.h"

#include <commctrl.h>
#include <shellapi.h>

#include <cstring>
#include <string>
#include <vector>

namespace
{
    // "/registrieren" und "-registrieren" gelten beide, Groß- und
    // Kleinschreibung gleichgültig.
    bool IsSwitch(const wchar_t* argument, const wchar_t* name)
    {
        if (argument == nullptr || (argument[0] != L'/' && argument[0] != L'-'))
            return false;
        return _wcsicmp(argument + 1, name) == 0;
    }

    // Trägt den gängigen Satz Endungen ein oder räumt sämtliche Einträge ab --
    // ohne Fenster und ohne Meldung. Wer das aus einem Skript aufruft, will
    // keinen Kasten wegklicken, sondern einen Rückgabewert lesen: 0 geglückt,
    // 1 nicht. In welchen Zweig geschrieben wird, entscheiden auch hier die
    // Rechte des Laufs.
    int RunSwitch(bool registering)
    {
        std::vector<std::wstring> wanted;
        if (registering)
        {
            for (const FileType& type : FileTypes())
            {
                if (type.standard)
                    wanted.emplace_back(type.extension);
            }
        }

        std::wstring error;
        return ApplyRegistration(ProcessScope(), wanted, error) ? 0 : 1;
    }
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int showCmd)
{
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr))
        return 1;

    std::wstring initialFile;
    int switchResult = -1;   // -1: es war kein Schalter dabei
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc))
    {
        if (argc > 1)
        {
            if (IsSwitch(argv[1], L"registrieren"))
                switchResult = RunSwitch(true);
            else if (IsSwitch(argv[1], L"abmelden"))
                switchResult = RunSwitch(false);
            else
                initialFile = argv[1];
        }
        LocalFree(argv);
    }

    if (switchResult >= 0)
    {
        CoUninitialize();
        return switchResult;
    }

    // Wird für das Tooltip-Fenster der Icon-Leiste gebraucht.
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&controls);

    int exitCode = 1;
    {
        MainWindow window;
        if (window.Create(instance, showCmd))
        {
            if (!initialFile.empty())
                window.OpenFile(initialFile);
            exitCode = window.RunMessageLoop();
        }
    }

    CoUninitialize();
    return exitCode;
}
