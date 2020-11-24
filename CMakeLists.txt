cmake_minimum_required(VERSION 3.10)
project(pimidid)

enable_language(C)

find_package(ALSA REQUIRED)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR}/cmake)

# https://github.com/rpavlik/cmake-modules/blob/master/Findudev.cmake
find_package(UDEV REQUIRED)

add_executable(pimidid pimidid.c pimididd.c pimidid.h)

target_compile_definitions(pimidid PUBLIC -D_POSIX_C_SOURCE=199309L -D_GNU_SOURCE)
target_include_directories(pimidid PUBLIC ${ALSA_INCLUDE_DIRS} ${UDEV_INCLUDE_DIRS})
target_link_libraries(pimidid ${ALSA_LIBRARIES} ${UDEV_LIBRARIES})
