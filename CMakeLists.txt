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

if(WIN32)
    set_target_properties(ogr2postgis_gui PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# Code signing (optional). Select the certificate with one of:
#   CODESIGN_CERT_SUBJECT     subject name in the Windows certificate store (signtool /n)
#   CODESIGN_CERT_THUMBPRINT  SHA1 thumbprint in the Windows certificate store (signtool /sha1)
#   CODESIGN_PFX              .pfx file, optionally with CODESIGN_PFX_PASSWORD (signtool /f)
# When one of them is set the executables are signed after linking and the
# installer after packaging (see cmake/SignPackage.cmake). Otherwise signing
# is skipped. Example:
#   cmake -B build -DCODESIGN_CERT_SUBJECT="MapCentia ApS"
set(CODESIGN_CERT_SUBJECT "" CACHE STRING "Subject name of the code signing certificate in the Windows certificate store")
set(CODESIGN_CERT_THUMBPRINT "" CACHE STRING "SHA1 thumbprint of the code signing certificate in the Windows certificate store")
set(CODESIGN_PFX "" CACHE FILEPATH "Code signing certificate as .pfx file")
set(CODESIGN_PFX_PASSWORD "" CACHE STRING "Password for CODESIGN_PFX")
set(CODESIGN_TIMESTAMP_URL "http://timestamp.digicert.com" CACHE STRING "RFC 3161 timestamp server")

set(CODESIGN_COMMAND "")
if(WIN32 AND (CODESIGN_CERT_SUBJECT OR CODESIGN_CERT_THUMBPRINT OR CODESIGN_PFX))
    set(_pf86 "ProgramFiles(x86)") # parentheses are not allowed directly in $ENV{}
    file(GLOB _sdk_bin_dirs LIST_DIRECTORIES true "$ENV{${_pf86}}/Windows Kits/10/bin/10.*/x64")
    list(SORT _sdk_bin_dirs ORDER DESCENDING)
    find_program(SIGNTOOL_EXECUTABLE signtool HINTS "$ENV{WindowsSdkVerBinPath}/x64" ${_sdk_bin_dirs})
    if(NOT SIGNTOOL_EXECUTABLE)
        message(FATAL_ERROR "Code signing requested but signtool.exe was not found. Install the Windows SDK.")
    endif()
    set(CODESIGN_COMMAND "${SIGNTOOL_EXECUTABLE}" sign /fd SHA256 /td SHA256
            /tr "${CODESIGN_TIMESTAMP_URL}" /d "${PROJECT_NAME}")
    if(CODESIGN_CERT_SUBJECT)
        list(APPEND CODESIGN_COMMAND /n "${CODESIGN_CERT_SUBJECT}")
    elseif(CODESIGN_CERT_THUMBPRINT)
        list(APPEND CODESIGN_COMMAND /sha1 "${CODESIGN_CERT_THUMBPRINT}")
    else()
        list(APPEND CODESIGN_COMMAND /f "${CODESIGN_PFX}")
        if(CODESIGN_PFX_PASSWORD)
            list(APPEND CODESIGN_COMMAND /p "${CODESIGN_PFX_PASSWORD}")
        endif()
    endif()
    foreach(tgt ogr2postgis ogr2postgis_gui)
        add_custom_command(TARGET ${tgt} POST_BUILD
                COMMAND ${CODESIGN_COMMAND} "$<TARGET_FILE:${tgt}>"
                COMMENT "Signing ${tgt}")
    endforeach()
    message(STATUS "Code signing enabled with ${SIGNTOOL_EXECUTABLE}")
else()
    message(STATUS "Code signing disabled (set CODESIGN_CERT_SUBJECT, CODESIGN_CERT_THUMBPRINT or CODESIGN_PFX to enable)")
endif()

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

if(WIN32)
    # Bundle the vcpkg DLLs next to the executables.
    install(TARGETS ogr2postgis ogr2postgis_gui
            RUNTIME_DEPENDENCY_SET ogr2postgis_runtime_deps
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    install(RUNTIME_DEPENDENCY_SET ogr2postgis_runtime_deps
            DESTINATION ${CMAKE_INSTALL_BINDIR}
            PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*"
            POST_EXCLUDE_REGEXES ".*[Ss]ystem32.*\\.dll"
            DIRECTORIES "$<TARGET_FILE_DIR:ogr2postgis_gui>")
else()
    # On Linux the shared libraries come from the distribution packages.
    install(TARGETS ogr2postgis ogr2postgis_gui
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VENDOR "ogr2postgis")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "OGR to PostGIS import tools")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "${PROJECT_NAME}")

# Release configuration for CPack
set(CPACK_BUILD_CONFIG "Release")
set(CPACK_STRIP_FILES TRUE)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_PACKAGE_NAME "${PROJECT_NAME}")
    set(CPACK_NSIS_DISPLAY_NAME "${PROJECT_NAME}")
    set(CPACK_NSIS_MODIFY_PATH ON)
else()
    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGE_CONTACT "mh@mapcentia.com")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Martin Høgh")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
endif()

# Sign the generated installer after CPack has built it.
set(CPACK_CODESIGN_COMMAND "${CODESIGN_COMMAND}")
set(CPACK_POST_BUILD_SCRIPTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SignPackage.cmake")

include(CPack)


