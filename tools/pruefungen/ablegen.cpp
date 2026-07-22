// Prueft die Pfadentnahme aus MainWindow::OnDropFiles an einem echten HDROP.
//
// Der Ablegevorgang selbst laesst sich von aussen nicht ausloesen: die Shell
// legt das HDROP im Zielprozess an, und ein Handle aus einem fremden Prozess
// ist dort wertlos. Was hier geprueft wird, ist der Teil mit echtem Risiko --
// die Groessenrechnung der Puffer. DragQueryFileW meldet die Laenge ohne
// Abschluss, will aber die Puffergroesse mit Abschluss.
//
// Geprueft werden eine Datei, mehrere auf einmal, Umlaute und Leerzeichen, ein
// Ordner und ein Pfad jenseits von MAX_PATH. Das Programm meldet selbst, ob
// alle Faelle bestanden sind -- Rueckgabewert 0 heisst ja. Siehe PLAN.md,
// Abschnitt 6.

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>   // DROPFILES

#include <clocale>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    // Baut ein HDROP, wie es die Shell beim Ablegen ueberreicht.
    HDROP MakeDrop(const std::vector<std::wstring>& paths)
    {
        std::wstring block;
        for (const std::wstring& path : paths)
        {
            block += path;
            block += L'\0';
        }
        block += L'\0';   // doppelter Abschluss der Liste

        const size_t bytes = sizeof(DROPFILES) + block.size() * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (memory == nullptr)
            return nullptr;

        auto* header = static_cast<DROPFILES*>(GlobalLock(memory));
        header->pFiles = sizeof(DROPFILES);
        header->pt = POINT{ 0, 0 };
        header->fNC = FALSE;
        header->fWide = TRUE;
        memcpy(reinterpret_cast<BYTE*>(header) + sizeof(DROPFILES), block.data(),
               block.size() * sizeof(wchar_t));
        GlobalUnlock(memory);

        return static_cast<HDROP>(memory);
    }

    // Wortgleich mit MainWindow::OnDropFiles.
    std::wstring Extract(HDROP drop)
    {
        std::wstring taken;
        const UINT length = DragQueryFileW(drop, 0, nullptr, 0);
        if (length > 0)
        {
            std::wstring path(length + 1, L'\0');
            if (DragQueryFileW(drop, 0, path.data(), length + 1) > 0)
            {
                path.resize(length);
                taken = path;
            }
        }
        DragFinish(drop);
        return taken;
    }

    int failures = 0;

    void Check(const std::vector<std::wstring>& paths, const std::wstring& expected)
    {
        HDROP drop = MakeDrop(paths);
        const std::wstring got = Extract(drop);

        const bool ok = got == expected && got.size() == expected.size();
        if (!ok)
            ++failures;
        wprintf(L"  %-5s %2zu Datei(en) -> \"%s\"  (Laenge %zu)\n",
                ok ? L"ok" : L"FEHL", paths.size(), got.c_str(), got.size());
    }
}

int wmain()
{
    // Ohne das gaebe wprintf die Umlaute in der Zeichentabelle der Konsole aus
    // -- und einer der Faelle handelt gerade von Umlauten. Die Pruefung selbst
    // vergliche zwar richtig, nur waere ihr Ausdruck nicht zu lesen.
    setlocale(LC_ALL, ".UTF8");

    wprintf(L"Pfadentnahme aus einem HDROP:\n");

    Check({ L"E:\\bilder\\foto.jpg" }, L"E:\\bilder\\foto.jpg");

    // Mehrere Dateien: die erste zaehlt, die uebrigen liegen im selben Ordner.
    Check({ L"E:\\bilder\\a.png", L"E:\\bilder\\b.png", L"E:\\bilder\\c.png" },
          L"E:\\bilder\\a.png");

    // Umlaute und Leerzeichen -- die Laenge zaehlt in Zeichen, nicht in Bytes.
    Check({ L"E:\\Meine Bilder\\Grüße aus München.jpeg" },
          L"E:\\Meine Bilder\\Grüße aus München.jpeg");

    // Ein Ordner kommt genauso an wie eine Datei; OpenFile faengt ihn ab.
    Check({ L"E:\\bilder" }, L"E:\\bilder");

    // Laenger als MAX_PATH.
    Check({ L"E:\\" + std::wstring(400, L'x') + L"\\bild.png" },
          L"E:\\" + std::wstring(400, L'x') + L"\\bild.png");

    wprintf(failures == 0 ? L"\nAlle Faelle bestanden.\n" : L"\n%d Faelle fehlgeschlagen.\n",
            failures);
    return failures == 0 ? 0 : 1;
}
