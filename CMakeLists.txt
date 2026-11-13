cmake_minimum_required(VERSION 3.21)
# The icon resource (.rc) has no includes; skip cmcldeps, which breaks with some CMake/Ninja setups.
set(CMAKE_NINJA_CMCLDEPS_RC OFF)
project(Pauta VERSION 0.8.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui PrintSupport Xml Network)
qt_standard_project_setup()

# Qt's private zip reader (QZipReader) opens compressed MusicXML (.mxl). It lives in QtCore
# since Qt 6.5 and in QtGui before that, so use whichever private module targets exist
# (Qt >= 6.9 needs them requested explicitly; older Qt defines them with Core/Gui).
find_package(Qt6 QUIET COMPONENTS CorePrivate GuiPrivate)

qt_add_executable(Pauta WIN32
    src/main.cpp
    src/model/Fraction.h
    src/model/Score.h
    src/model/Score.cpp
    src/io/NativeFormat.h
    src/io/NativeFormat.cpp
    src/io/MusicXml.h
    src/io/MusicXml.cpp
    src/render/Smufl.h
    src/render/Smufl.cpp
    src/render/Layout.h
    src/render/Engraver.h
    src/render/Engraver.cpp
    src/render/Renderer.h
    src/render/Renderer.cpp
    src/playback/Sequence.h
    src/playback/Sequence.cpp
    src/playback/MidiOut.h
    src/playback/MidiOut.cpp
    src/playback/Player.h
    src/playback/Player.cpp
    src/update/Updater.h
    src/update/Updater.cpp
    src/edit/Editor.h
    src/edit/Editor.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/ScoreView.h
    src/ui/ScoreView.cpp
    src/ui/NewScoreDialog.h
    src/ui/NewScoreDialog.cpp
    src/ui/Tools.h
    src/ui/Icons.h
    src/ui/Icons.cpp
    src/ui/LaunchDialog.h
    src/ui/LaunchDialog.cpp
)

qt_add_resources(Pauta "resources"
    PREFIX "/"
    FILES resources/fonts/Bravura.otf resources/pauta.png CHANGELOG.md
)

target_include_directories(Pauta PRIVATE src)
target_compile_definitions(Pauta PRIVATE PROJECT_VERSION_STRING="${PROJECT_VERSION}")
# GitHub repository checked for new versions (Releases).
set(PAUTA_GITHUB_REPO "Leandrocru/pauta" CACHE STRING "owner/name of the GitHub repository with the releases")
target_compile_definitions(Pauta PRIVATE PAUTA_GITHUB_REPO="${PAUTA_GITHUB_REPO}")
target_link_libraries(Pauta PRIVATE Qt6::Widgets Qt6::Gui Qt6::PrintSupport Qt6::Xml Qt6::Network)
if(TARGET Qt6::CorePrivate AND TARGET Qt6::GuiPrivate)
    target_link_libraries(Pauta PRIVATE Qt6::CorePrivate Qt6::GuiPrivate)
else()
    target_compile_definitions(Pauta PRIVATE PAUTA_NO_MXL)
    message(WARNING "Cabecalhos privados do Qt nao encontrados: arquivos .mxl compactados nao serao suportados")
endif()

if(WIN32)
    target_sources(Pauta PRIVATE resources/pauta.rc) # icon of Pauta.exe
    target_link_libraries(Pauta PRIVATE winmm)       # built-in MIDI synthesizer for playback
elseif(UNIX AND NOT APPLE)
    find_package(ALSA)
    if(ALSA_FOUND)
        target_sources(Pauta PRIVATE src/playback/SoftSynth.h src/playback/SoftSynth.cpp)
        target_link_libraries(Pauta PRIVATE ALSA::ALSA)
        target_compile_definitions(Pauta PRIVATE PAUTA_HAVE_ALSA)
    else()
        message(WARNING "ALSA nao encontrado: reproducao de som desativada")
    endif()
endif()

if(MSVC)
    target_compile_options(Pauta PRIVATE /W4 /permissive- /utf-8)
endif()

# ---- Linux: installation layout, .deb (CPack) and the AppDir used for the AppImage ----------
if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    set_target_properties(Pauta PROPERTIES OUTPUT_NAME pauta)
    install(TARGETS Pauta RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES packaging/linux/pauta.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES resources/pauta.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
    install(FILES packaging/linux/pauta-mime.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages RENAME pauta.xml)

    set(CPACK_GENERATOR DEB)
    set(CPACK_PACKAGE_NAME pauta)
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Editor de partituras leve")
    set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "Editor de partituras leve, inspirado no fluxo de trabalho do Finale.")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Projeto Pauta")
    set(CPACK_DEBIAN_PACKAGE_SECTION sound)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
    include(CPack)
endif()
