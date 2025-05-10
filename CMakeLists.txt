cmake_minimum_required(VERSION 3.20)

project(videoplayer)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# enable moc uic rcc
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
file(GLOB SOURCES
        *.cpp
        *.h
)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Network Multimedia)
link_libraries(Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network Qt5::Multimedia)
link_directories(install/lib)
link_libraries(avformat avcodec avutil swscale
        swresample avdevice avfilter postproc avformat yuv GL)
include_directories(/usr/include/ffmpeg install/include)
add_subdirectory(player)
include_directories(.)
add_executable(${PROJECT_NAME} ${SOURCES})
#add_subdirectory(tests)
