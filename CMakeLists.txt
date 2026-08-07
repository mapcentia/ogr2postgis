cmake_minimum_required(VERSION 3.28)
project(ogr2postgis)

set(CMAKE_CXX_STANDARD 23)

find_package(GDAL CONFIG REQUIRED)
find_package(wxWidgets COMPONENTS gl core base REQUIRED)

include(${wxWidgets_USE_FILE})

add_executable(ogr2postgis src/ogr2postgis.cpp)
add_executable(ogr2postgis_gui src/ogr2postgis_gui.cpp)

target_compile_definitions(ogr2postgis_gui PRIVATE wxUSE_RC_MANIFEST=1 ${wxWidgets_DEFINITIONS} "$<$<CONFIG:DEBUG>:${wxWidgets_DEFINITIONS_DEBUG}>")
target_include_directories(ogr2postgis_gui PRIVATE ${wxWidgets_INCLUDE_DIRS})

target_link_libraries(ogr2postgis GDAL::GDAL)
target_link_libraries(ogr2postgis_gui ${wxWidgets_LIBRARIES} GDAL::GDAL)

set_target_properties(ogr2postgis_gui PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS")


