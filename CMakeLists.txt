cmake_minimum_required(VERSION 3.28)
project(ogr2postgis VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
include(GNUInstallDirs)

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

# GDAL and PROJ from vcpkg have no compiled-in data path on Windows. Ship their
# data directories as "gdal-data" and "proj" next to the executables; the
# programs point GDAL/PROJ at them on startup (see configureDataPaths()).
set(GDAL_DATA_SRC "${GDAL_DIR}")
set(PROJ_DATA_SRC "${GDAL_DIR}/../proj")
if(EXISTS "${GDAL_DATA_SRC}/gml_registry.xml" AND EXISTS "${PROJ_DATA_SRC}/proj.db")
    foreach(tgt ogr2postgis ogr2postgis_gui)
        add_custom_command(TARGET ${tgt} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                "${GDAL_DATA_SRC}" "$<TARGET_FILE_DIR:${tgt}>/gdal-data"
                COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                "${PROJ_DATA_SRC}" "$<TARGET_FILE_DIR:${tgt}>/proj"
                COMMENT "Copying GDAL and PROJ data next to ${tgt}")
    endforeach()
    install(DIRECTORY "${GDAL_DATA_SRC}/" DESTINATION ${CMAKE_INSTALL_BINDIR}/gdal-data
            PATTERN "*.cmake" EXCLUDE PATTERN "*.svg" EXCLUDE)
    install(DIRECTORY "${PROJ_DATA_SRC}/" DESTINATION ${CMAKE_INSTALL_BINDIR}/proj
            PATTERN "*.cmake" EXCLUDE)
else()
    message(STATUS "GDAL/PROJ data directories not found next to GDAL_DIR; relying on compiled-in paths")
endif()

# Bundle the Visual C++ runtime (vcruntime140.dll, msvcp140.dll, ...) so end
# users do not need the VC++ Redistributable installed.
if(MSVC)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
    include(InstallRequiredSystemLibraries)
endif()

install(TARGETS ogr2postgis ogr2postgis_gui
        RUNTIME_DEPENDENCY_SET ogr2postgis_runtime_deps
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(RUNTIME_DEPENDENCY_SET ogr2postgis_runtime_deps
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*"
        POST_EXCLUDE_REGEXES ".*[Ss]ystem32.*\\.dll"
        DIRECTORIES "$<TARGET_FILE_DIR:ogr2postgis_gui>")

set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VENDOR "ogr2postgis")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "OGR to PostGIS import tools")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "${PROJECT_NAME}")
set(CPACK_GENERATOR "NSIS")
set(CPACK_NSIS_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_NSIS_DISPLAY_NAME "${PROJECT_NAME}")
set(CPACK_NSIS_MODIFY_PATH ON)

include(CPack)


