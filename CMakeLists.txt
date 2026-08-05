cmake_minimum_required(VERSION 3.21)

# Diese Nummer ist die einzige Stelle, an der die Version steht. Die
# Ressourcendatei bekommt sie über res/Version.h.in zugereicht.
project(Bildanzeige VERSION 1.5.1 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Version.h und app.manifest tragen beide die Versionsnummer und werden deshalb
# aus Vorlagen erzeugt, statt sie von Hand mitzuführen.
configure_file(res/Version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/Version.h @ONLY)
configure_file(res/app.manifest.in ${CMAKE_CURRENT_BINARY_DIR}/generated/app.manifest @ONLY)

add_executable(Bildanzeige WIN32
    src/main.cpp
    src/Common.cpp
    src/MainWindow.cpp
    src/ImageDocument.cpp
    src/FolderNavigator.cpp
    src/DecodeWorker.cpp
    src/GifAnimator.cpp
    src/ViewState.cpp
    src/RenderView.cpp
    src/Toolbar.cpp
    src/Printer.cpp
    src/PrintPreview.cpp
    src/FileAssociation.cpp
    src/AssociationDialog.cpp
    src/PrintDialogSetting.cpp
    src/SettingsDialog.cpp
    res/Bildanzeige.rc
)

# res/ liegt im Include-Pfad, damit die .rc das Symbol und die Kennungen aus
# Resource.h findet; generated/ liefert Version.h und app.manifest. Der
# Ressourcencompiler sucht zuerst neben der .rc selbst und erst dann hier --
# eine gleichnamige Datei in res/ würde die erzeugte also verdecken.
target_include_directories(Bildanzeige PRIVATE
    src res ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# Was die .rc einbindet, sieht die Abhängigkeitsverfolgung nicht von selbst:
# für .rc-Dateien entsteht keine Abhängigkeitsliste wie beim Übersetzen.
# Ohne diese Zeile bliebe nach einer neuen Versionsnummer oder einem neuen
# Symbol die alte Ressource in der EXE stehen -- und zwar lautlos.
set_source_files_properties(res/Bildanzeige.rc PROPERTIES OBJECT_DEPENDS
    "${CMAKE_CURRENT_BINARY_DIR}/generated/Version.h;${CMAKE_CURRENT_BINARY_DIR}/generated/app.manifest;${CMAKE_CURRENT_SOURCE_DIR}/res/icon.ico;${CMAKE_CURRENT_SOURCE_DIR}/res/Resource.h"
)

target_link_libraries(Bildanzeige PRIVATE
    d2d1 windowscodecs ole32 shlwapi shell32 user32 comctl32 comdlg32 gdi32 winspool advapi32
    dwmapi
)

target_compile_definitions(Bildanzeige PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00
)

if (MSVC)
    target_compile_options(Bildanzeige PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/W4 /permissive- /utf-8 /guard:cf>
    )
    # Das Manifest wird über die .rc eingebettet; der Linker darf kein zweites
    # erzeugen. /guard:cf muss auch der Linker sehen, sonst trägt die EXE die
    # Sprungtabelle zwar in sich, meldet sie aber nicht an.
    #
    # /CETCOMPAT meldet die EXE für den Schattenstapel der CPU an (Hardware-
    # enforced Stack Protection) -- die Exploit-Klasse, die bei einem Fehler in
    # einem Bild-Codec zum Tragen käme; Prozessoren ohne CET übergehen es.
    # /DEPENDENTLOADFLAG:0x800 lässt die importierten DLLs nur noch aus
    # System32 auflösen: eine gleichnamige DLL neben der EXE liefe sonst mit.
    # Alle Importe hier sind System-DLLs, die EXE selbst verliert also nichts.
    target_link_options(Bildanzeige PRIVATE /MANIFEST:NO /guard:cf /CETCOMPAT
        /DEPENDENTLOADFLAG:0x800)
    # Statische CRT -> eine einzelne, portable EXE ohne Redistributable.
    set_property(TARGET Bildanzeige PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    # Optimierung über Übersetzungseinheiten hinweg (LTCG) war hier auch
    # eingebaut und ist wieder herausgeflogen: dreimal aus derselben Quelle
    # gebaut ergab sie 512 Byte *mehr* und keinen messbaren Unterschied an
    # Bauzeit. Bei zehn kleinen Übersetzungseinheiten, deren Arbeit ohnehin in
    # WIC und Direct2D stattfindet, gibt es nichts zu gewinnen.
endif()
