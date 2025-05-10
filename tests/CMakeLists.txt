

include_directories(.)
add_compile_definitions(CURRENT_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(tests_pcm audiodecode.cpp)
add_executable(tests_pcm2 audioresample.cpp)
add_executable(tests_video videodecode.cpp)