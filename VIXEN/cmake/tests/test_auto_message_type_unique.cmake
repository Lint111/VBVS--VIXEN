cmake_minimum_required(VERSION 3.20)

# AUTO_MESSAGE_TYPE() hashes the definition site's path-free file name and line, so two sites with
# the same basename and line in different directories would share a message type id. Fail when any
# basename:line pair repeats.

if(NOT DEFINED VIXEN_SOURCE_DIR)
    message(FATAL_ERROR "VIXEN_SOURCE_DIR is required")
endif()

file(GLOB_RECURSE _sources
    "${VIXEN_SOURCE_DIR}/libraries/*.h" "${VIXEN_SOURCE_DIR}/libraries/*.hpp"
    "${VIXEN_SOURCE_DIR}/libraries/*.cpp"
    "${VIXEN_SOURCE_DIR}/application/*.h" "${VIXEN_SOURCE_DIR}/application/*.hpp"
    "${VIXEN_SOURCE_DIR}/application/*.cpp")

set(_seen_keys)
set(_site_count 0)
set(_collisions)
foreach(_source IN LISTS _sources)
    get_filename_component(_name "${_source}" NAME)
    file(STRINGS "${_source}" _lines)
    set(_line_number 0)
    foreach(_line IN LISTS _lines)
        math(EXPR _line_number "${_line_number} + 1")
        if(_line MATCHES "AUTO_MESSAGE_TYPE\\(\\)" AND NOT _line MATCHES "#[ \t]*define")
            math(EXPR _site_count "${_site_count} + 1")
            set(_key "${_name}:${_line_number}")
            if(_key IN_LIST _seen_keys)
                list(APPEND _collisions "${_key} (again in ${_source})")
            endif()
            list(APPEND _seen_keys "${_key}")
        endif()
    endforeach()
endforeach()

if(_site_count EQUAL 0)
    message(FATAL_ERROR "no AUTO_MESSAGE_TYPE() sites found under ${VIXEN_SOURCE_DIR}; the scan is broken")
endif()
if(_collisions)
    list(JOIN _collisions "\n  " _collision_text)
    message(FATAL_ERROR "AUTO_MESSAGE_TYPE() sites share basename and line:\n  ${_collision_text}")
endif()
message(STATUS "AUTO_MESSAGE_TYPE(): ${_site_count} sites, no basename+line collisions")
