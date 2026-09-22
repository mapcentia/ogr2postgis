# CPack post-build script: signs every generated package (NSIS installer, ...)
# with the signtool command assembled in CMakeLists.txt. Runs in the cpack
# process, where CPACK_PACKAGE_FILES lists the packages just built.
if(NOT CPACK_CODESIGN_COMMAND)
    return()
endif()

foreach(pkg IN LISTS CPACK_PACKAGE_FILES)
    if(NOT pkg MATCHES "\\.(exe|msi)$")
        continue()
    endif()
    message(STATUS "Signing ${pkg}")
    execute_process(
            COMMAND ${CPACK_CODESIGN_COMMAND} "${pkg}"
            RESULT_VARIABLE res
            OUTPUT_VARIABLE out
            ERROR_VARIABLE err)
    if(NOT res EQUAL 0)
        message(FATAL_ERROR "Signing ${pkg} failed:\n${out}\n${err}")
    endif()
endforeach()
