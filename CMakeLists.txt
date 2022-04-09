cmake_minimum_required(VERSION 3.13)
project(ogr2postgis)

set(CMAKE_CXX_STANDARD 20)

add_executable(ogr2postgis src/ogr2postgis.cpp)
include_directories(/usr)

target_link_libraries(ogr2postgis gdal stdc++fs)

install(TARGETS ogr2postgis DESTINATION bin)
