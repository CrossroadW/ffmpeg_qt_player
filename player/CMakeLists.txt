
find_package(Boost CONFIG COMPONENTS REQURIED thread)
file(GLOB SOURCES *.cpp *.h)
add_executable(player ${SOURCES})
include_directories(/usr/include/ffmpeg)
find_package(spdlog CONFIG REQUIRED)

target_link_libraries(player PRIVATE Boost::thread spdlog::spdlog)