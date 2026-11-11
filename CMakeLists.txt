cmake_minimum_required(VERSION 3.21)
# The icon resource (.rc) has no includes; skip cmcldeps, which breaks with some CMake/Ninja setups.
set(CMAKE_NINJA_CMCLDEPS_RC OFF)
project(Pauta VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui PrintSupport Xml)
qt_standard_project_setup()

# Qt's private zip reader (QZipReader, exported by QtCore) opens compressed MusicXML (.mxl).
# Its header lives in a versioned folder that differs between Qt builds, so look for it.
set(PAUTA_ZIP_INCLUDE "")
get_target_property(_qtcore_incs Qt6::Core INTERFACE_INCLUDE_DIRECTORIES)
foreach(_d IN LISTS _qtcore_incs)
    if(NOT PAUTA_ZIP_INCLUDE AND NOT _d MATCHES "\\$<" AND IS_DIRECTORY "${_d}")
        file(GLOB_RECURSE _zip LIST_DIRECTORIES false "${_d}/qzipreader_p.h")
        if(_zip)
            list(GET _zip 0 _zip)
            get_filename_component(_zip "${_zip}" DIRECTORY) # .../private
            get_filename_component(_zip "${_zip}" DIRECTORY) # .../QtCore (versioned)
            set(PAUTA_ZIP_INCLUDE "${_zip}")
        endif()
    endif()
endforeach()

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
    FILES resources/fonts/Bravura.otf resources/pauta.png
)

target_include_directories(Pauta PRIVATE src)
target_compile_definitions(Pauta PRIVATE PROJECT_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(Pauta PRIVATE Qt6::Widgets Qt6::Gui Qt6::PrintSupport Qt6::Xml)
if(PAUTA_ZIP_INCLUDE)
    target_include_directories(Pauta SYSTEM PRIVATE "${PAUTA_ZIP_INCLUDE}")
    message(STATUS "QZipReader: ${PAUTA_ZIP_INCLUDE}")
else()
    target_compile_definitions(Pauta PRIVATE PAUTA_NO_MXL)
    message(WARNING "Cabecalhos privados do Qt nao encontrados: arquivos .mxl compactados nao serao suportados")
endif()

if(WIN32)
    target_sources(Pauta PRIVATE resources/pauta.rc) # icon of Pauta.exe
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
