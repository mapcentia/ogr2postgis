cmake_minimum_required(VERSION 3.13)
project(ogr2postgis)

set(CMAKE_CXX_STANDARD 17)

add_executable(ogr2postgis main.cpp)

target_link_libraries(ogr2postgis gdal stdc++fs)