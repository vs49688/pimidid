cmake_minimum_required(VERSION 3.10)
project(pimidid)

enable_language(C)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR}/cmake)

find_package(ALSA REQUIRED)
add_library(ALSA INTERFACE)
target_link_libraries(ALSA INTERFACE ${ALSA_LIBRARIES})
target_include_directories(ALSA INTERFACE ${ALSA_INCLUDE_DIRS})


# https://github.com/rpavlik/cmake-modules/blob/master/Findudev.cmake
find_package(UDEV REQUIRED)
add_library(udev INTERFACE)
target_link_libraries(udev INTERFACE ${UDEV_LIBRARIES})
target_include_directories(udev INTERFACE ${UDEV_INCLUDE_DIRS})

add_executable(pimidid pimidid.c pimididd.c pimidid.h)

target_compile_definitions(pimidid PUBLIC -D_POSIX_C_SOURCE=199309L -D_GNU_SOURCE)
target_link_libraries(pimidid ALSA udev)
