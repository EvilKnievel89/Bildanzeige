#pragma once

#include "Common.h"

#include <string>
#include <vector>

// Ein Dateityp, für den sich die Anwendung eintragen kann.
struct FileType
{
    const wchar_t* extension;   // klein geschrieben, mit Punkt
    const wchar_t* name;        // steht im Explorer in der Spalte "Typ"
    bool standard;              // gehört zum Satz, den /registrieren einträgt
};

// Wohin die Einträge gehen. Das ist nichts, was der Benutzer im Fenster wählt:
// es hängt daran, mit welchen Rechten das Programm läuft. Erhöht gestartet
// schreibt es für alle Benutzer des Rechners, sonst für sich selbst.
enum class RegistryScope
{
    CurrentUser,
    LocalMachine,
};

// Die Formate, für die sich die Anwendung eintragen kann.
const std::vector<FileType>& FileTypes();

// Der Zweig, in den dieser Lauf schreiben darf.
RegistryScope ProcessScope();

// "HKEY_CURRENT_USER" bzw. "HKEY_LOCAL_MACHINE", für Meldungen und Beschriftung.
const wchar_t* ScopeKeyName(RegistryScope scope);

// Vollständiger Pfad der laufenden EXE; leer, wenn er sich nicht ermitteln ließ.
const std::wstring& ExecutablePath();

// Steht die Endung in diesem Zweig auf die Bildanzeige eingetragen?
bool IsRegistered(RegistryScope scope, const std::wstring& extension);

// Öffnet Windows diese Endung gerade mit dieser EXE? Das ist die Frage nach dem
// Standard und nicht nach dem Eintrag: eingetragen sein heißt nur, in "Öffnen
// mit" zu stehen -- siehe ApplyRegistration.
bool IsDefaultHandler(const std::wstring& extension);

// Trägt genau die genannten Endungen ein und nimmt jede andere zurück. Eine
// leere Liste räumt sämtliche Spuren der Anwendung aus dem Zweig.
bool ApplyRegistration(RegistryScope scope, const std::vector<std::wstring>& wanted,
                       std::wstring& error);

// Führt zu den Windows-Einstellungen "Standard-Apps". Den Standard selbst zu
// setzen ist seit Windows 8 keiner Anwendung mehr erlaubt; die Wahl trifft der
// Benutzer dort.
bool ShowDefaultAppsUI(RegistryScope scope, std::wstring& error);
