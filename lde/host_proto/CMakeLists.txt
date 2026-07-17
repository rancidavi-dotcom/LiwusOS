cmake_minimum_required(VERSION 3.10)
project(lde_proto)

set(CMAKE_C_STANDARD 99)

find_package(SDL2 REQUIRED)

add_executable(lde_proto main.c mock_bridge.c)
target_include_directories(lde_proto PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(lde_proto PRIVATE ${SDL2_LIBRARIES} m)
