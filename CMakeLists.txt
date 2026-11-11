cmake_minimum_required(VERSION 3.21)
project(Pauta VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui PrintSupport)
qt_standard_project_setup()

qt_add_executable(Pauta WIN32
    src/main.cpp
    src/model/Fraction.h
    src/model/Score.h
    src/model/Score.cpp
    src/io/NativeFormat.h
    src/io/NativeFormat.cpp
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

qt_add_resources(Pauta "fonts"
    PREFIX "/"
    FILES resources/fonts/Bravura.otf
)

target_include_directories(Pauta PRIVATE src)
target_compile_definitions(Pauta PRIVATE PROJECT_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(Pauta PRIVATE Qt6::Widgets Qt6::Gui Qt6::PrintSupport)

if(MSVC)
    target_compile_options(Pauta PRIVATE /W4 /permissive- /utf-8)
endif()
