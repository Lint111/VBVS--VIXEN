file(REMOVE_RECURSE "@ERROR_CATALOG_COLLISION_WORK_DIR@")
file(MAKE_DIRECTORY "@ERROR_CATALOG_COLLISION_WORK_DIR@")

execute_process(
    COMMAND @ERROR_CATALOG_RUNNER@ "@ERROR_CATALOG_DLL@"
        --error-catalog
        --error-fragment "@ERROR_CATALOG_KERNEL_FRAGMENT@"
        --error-fragment "@ERROR_CATALOG_VIXEN_COLLISION_FRAGMENT@"
        --out-json "@ERROR_CATALOG_COLLISION_JSON@"
        --docs-dir "@ERROR_CATALOG_COLLISION_DOCS@"
        --out-compat "@ERROR_CATALOG_COLLISION_COMPAT@"
        --out-cpp "@ERROR_CATALOG_COLLISION_HEADER@"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)

if(result EQUAL 0)
    message(FATAL_ERROR
        "VIXEN alias-collision fixture unexpectedly compiled successfully.\n${stdout}\n${stderr}")
endif()

set(diagnostics "${stdout}\n${stderr}")
foreach(required IN ITEMS
    "alias 'PIPE001'"
    "KernelErrorCatalogFragment.cs"
    "VixenAliasCollision.cs"
    "where:"
    "why:"
    "fix:")
    if(NOT diagnostics MATCHES "${required}")
        message(FATAL_ERROR
            "VIXEN alias-collision diagnostic omitted '${required}'.\n${diagnostics}")
    endif()
endforeach()

if(EXISTS "@ERROR_CATALOG_COLLISION_HEADER@")
    message(FATAL_ERROR
        "VIXEN alias-collision fixture emitted a C++ header after a compiler failure: @ERROR_CATALOG_COLLISION_HEADER@")
endif()

message(STATUS "PASS: VIXEN alias collision names both owners and emits no partial header")
