#include "MainWindow.h"

#include <commctrl.h>
#include <shellapi.h>

#include <string>

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int showCmd)
{
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr))
        return 1;

    // Wird für das Tooltip-Fenster der Icon-Leiste gebraucht.
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&controls);

    std::wstring initialFile;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc))
    {
        if (argc > 1)
            initialFile = argv[1];
        LocalFree(argv);
    }

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
