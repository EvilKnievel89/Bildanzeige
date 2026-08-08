// Bildanzeige -- Copyright (C) 2026 EvilKnievel89
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Freie Software unter der GNU GPL, Version 3 oder einer späteren: weitergeben
// und verändern ist erlaubt, solange Quelltext und Lizenz mitgehen. Ohne jede
// Gewährleistung. Wortlaut in LICENSE, Erklärung in LIZENZ.md.

// Prueft die Pfadentnahme aus MainWindow::OnDropFiles an einem echten HDROP.
//
// Der Ablegevorgang selbst lässt sich von außen nicht auslösen: die Shell
// legt das HDROP im Zielprozess an, und ein Handle aus einem fremden Prozess
// ist dort wertlos. Was hier geprüft wird, ist der Teil mit echtem Risiko --
// die Größenrechnung der Puffer. DragQueryFileW meldet die Länge ohne
// Abschluss, will aber die Puffergröße mit Abschluss.
//
// Geprüft werden eine Datei, mehrere auf einmal, Umlaute und Leerzeichen, ein
// Ordner und ein Pfad jenseits von MAX_PATH. Das Programm meldet selbst, ob
// alle Fälle bestanden sind -- Rückgabewert 0 heißt ja. Siehe PLAN.md,
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
    // Baut ein HDROP, wie es die Shell beim Ablegen überreicht.
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
        wprintf(L"  %-5s %2zu Datei(en) -> \"%s\"  (Länge %zu)\n",
                ok ? L"ok" : L"FEHL", paths.size(), got.c_str(), got.size());
    }
}

int wmain()
{
    // Ohne das gäbe wprintf die Umlaute in der Zeichentabelle der Konsole aus
    // -- und einer der Fälle handelt gerade von Umlauten. Die Prüfung selbst
    // vergliche zwar richtig, nur wäre ihr Ausdruck nicht zu lesen.
    setlocale(LC_ALL, ".UTF8");

    wprintf(L"Pfadentnahme aus einem HDROP:\n");

    Check({ L"E:\\bilder\\foto.jpg" }, L"E:\\bilder\\foto.jpg");

    // Mehrere Dateien: die erste zählt, die übrigen liegen im selben Ordner.
    Check({ L"E:\\bilder\\a.png", L"E:\\bilder\\b.png", L"E:\\bilder\\c.png" },
          L"E:\\bilder\\a.png");

    // Umlaute und Leerzeichen -- die Länge zählt in Zeichen, nicht in Bytes.
    Check({ L"E:\\Meine Bilder\\Grüße aus München.jpeg" },
          L"E:\\Meine Bilder\\Grüße aus München.jpeg");

    // Ein Ordner kommt genauso an wie eine Datei; OpenFile fängt ihn ab.
    Check({ L"E:\\bilder" }, L"E:\\bilder");

    // Länger als MAX_PATH.
    Check({ L"E:\\" + std::wstring(400, L'x') + L"\\bild.png" },
          L"E:\\" + std::wstring(400, L'x') + L"\\bild.png");

    wprintf(failures == 0 ? L"\nAlle Fälle bestanden.\n" : L"\n%d Fälle fehlgeschlagen.\n",
            failures);
    return failures == 0 ? 0 : 1;
}
