cmake_minimum_required(VERSION 3.0)

set(VCPKG_MANIFEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake CACHE STRING "Vcpkg toolchain file")

project(Project)

find_package(Boost REQUIRED COMPONENTS regex)

add_executable(main
main.cpp)

target_link_libraries(main PRIVATE Boost::regex)
