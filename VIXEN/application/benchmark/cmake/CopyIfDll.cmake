# CopyIfDll.cmake - Copy file only if it's a DLL
# Usage: cmake -DSRC=<source_file> -DDST=<dest_dir> -P CopyIfDll.cmake

if(NOT DEFINED SRC)
    message(FATAL_ERROR "SRC not defined")
endif()

if(NOT DEFINED DST)
    message(FATAL_ERROR "DST not defined")
endif()

# Check if file exists and is a DLL
if(EXISTS "${SRC}")
    get_filename_component(EXT "${SRC}" EXT)
    string(TOLOWER "${EXT}" EXT_LOWER)

    if("${EXT_LOWER}" STREQUAL ".dll")
        message(STATUS "Copying DLL: ${SRC} -> ${DST}")
        file(COPY "${SRC}" DESTINATION "${DST}")
    else()
        message(STATUS "Skipping non-DLL: ${SRC}")
    endif()
else()
    message(STATUS "File not found, skipping: ${SRC}")
endif()
