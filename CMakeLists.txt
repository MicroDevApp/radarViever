cmake_minimum_required(VERSION 3.16)
project(MapGlobe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Network OpenGL)

qt_add_executable(MapGlobe
    src/main.cpp
    src/TileCache.h
    src/TileCache.cpp
    src/MapView.h
    src/MapView.cpp
    src/MapRenderNode.h
    src/MapRenderNode.cpp
    resources.qrc
)

target_link_libraries(MapGlobe PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Network
    Qt6::OpenGL
)
