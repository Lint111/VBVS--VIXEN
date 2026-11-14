# Invoked by a queued native build, never at configure time. The shared binary
# entry is immutable after publication; build-tree copies are normal byproducts.
include("${VIXEN_CODEGEN_BUILD_CONFIG}")
file(MAKE_DIRECTORY "${VIXEN_CODEGEN_CACHE_DIR}")
file(LOCK "${VIXEN_CODEGEN_CACHE_DIR}/build.lock" GUARD PROCESS TIMEOUT 900 RESULT_VARIABLE _lock)
if(NOT _lock STREQUAL "0")
    message(FATAL_ERROR "[codegen] cannot lock cached CodegenTool: ${_lock}")
endif()
set(_ready "${VIXEN_CODEGEN_CACHE_DIR}/.build-complete")
if(NOT EXISTS "${_ready}" OR NOT EXISTS "${VIXEN_CODEGEN_CACHED_BIN}/CodegenTool.dll"
        OR NOT EXISTS "${VIXEN_CODEGEN_CACHED_BIN}/CodegenTool.deps.json"
        OR NOT EXISTS "${VIXEN_CODEGEN_CACHED_BIN}/CodegenTool.runtimeconfig.json")
    execute_process(COMMAND ${VIXEN_CODEGEN_BUILD_COMMAND} RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "[codegen] cached CodegenTool build failed: ${_result}")
    endif()
    foreach(_required CodegenTool.dll CodegenTool.deps.json CodegenTool.runtimeconfig.json)
        if(NOT EXISTS "${VIXEN_CODEGEN_CACHED_BIN}/${_required}")
            message(FATAL_ERROR "[codegen] CodegenTool build did not produce ${VIXEN_CODEGEN_CACHED_BIN}/${_required}")
        endif()
    endforeach()
    file(WRITE "${_ready}" "complete\n")
    message(STATUS "[codegen] built shared CodegenTool: ${VIXEN_CODEGEN_CACHED_BIN}")
else()
    message(STATUS "[codegen] reused shared CodegenTool: ${VIXEN_CODEGEN_CACHED_BIN}")
endif()
# One file(COPY) replaces the former per-file `cmake -E copy_if_different` child processes (47
# processes per fresh build tree); it skips files whose timestamp and size already match. The stamp
# records the cache entry (its directory name is the kernel/tool/RID/SDK/recipe identity key), so a
# build tree states which immutable tool it consumes. Checks and regen targets depend on this
# stamp and never restore or build the tool themselves.
file(COPY "${VIXEN_CODEGEN_CACHED_BIN}/." DESTINATION "${VIXEN_CODEGEN_LOCAL_BIN}")
foreach(_required CodegenTool.dll CodegenTool.deps.json CodegenTool.runtimeconfig.json)
    if(NOT EXISTS "${VIXEN_CODEGEN_LOCAL_BIN}/${_required}")
        message(FATAL_ERROR "[codegen] failed to copy cached tool binary: ${_required}")
    endif()
endforeach()
get_filename_component(_tool_key "${VIXEN_CODEGEN_CACHE_DIR}" NAME)
file(WRITE "${VIXEN_CODEGEN_LOCAL_STAMP}" "complete\ntool-key ${_tool_key}\n")
