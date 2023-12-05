cmake_minimum_required(VERSION 3.13)
project(ogr2postgis)

set(CMAKE_CXX_STANDARD 23)

# Specify the wx-config executable if not in the default system path
set(wxWidgets_CONFIG_EXECUTABLE "/usr/bin/wx-config")

find_package(wxWidgets REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})

add_executable(ogr2postgis src/ogr2postgis.cpp)
add_executable(ogr2postgis_gui src/ogr2postgis_gui.cpp)

target_link_libraries(ogr2postgis gdal pthread stdc++fs)
target_link_libraries(ogr2postgis_gui ${wxWidgets_LIBRARIES} gdal pthread stdc++fs )

install(TARGETS ogr2postgis DESTINATION bin)
install(TARGETS ogr2postgis_gui DESTINATION bin)
