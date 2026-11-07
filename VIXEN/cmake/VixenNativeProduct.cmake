# VIXEN's native provider closure for a KFR-owned immutable product.
#
# The renderer provider is consumed through the small NativeDependencies target.  Keep its
# install surface here so KFR does not duplicate VIXEN's dependency/header inventory, while KFR
# remains responsible for its own renderer archives and request metadata.
include_guard(GLOBAL)
include(GNUInstallDirs)

function(vixen_native_product_install)
    if(NOT TARGET VIXENNativeDependencies OR NOT TARGET KernelDispatch)
        message(FATAL_ERROR "VIXEN native product install requires VIXENNativeDependencies and KernelDispatch")
    endif()

    set(_vixen_targets VIXENNativeDependencies KernelDispatch)
    set(_vixen_binary_targets)
    foreach(_target IN ITEMS gaia glm nlohmann_json)
        if(TARGET "${_target}")
            list(APPEND _vixen_targets "${_target}")
        endif()
    endforeach()
    foreach(_target IN ITEMS tbb tbbmalloc tbbmalloc_proxy)
        if(TARGET "${_target}")
            list(APPEND _vixen_binary_targets "${_target}")
        endif()
    endforeach()

    install(TARGETS ${_vixen_targets}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    # Install each oneTBB library under its SONAME (libtbb.so.12, libtbbmalloc.so.2) as a regular
    # file: a consumer's DT_NEEDED entry names the SONAME, so the unversioned development name
    # alone leaves the product's own link unresolved at run time.
    foreach(_target IN LISTS _vixen_binary_targets)
        install(FILES "$<TARGET_FILE:${_target}>"
            DESTINATION ${CMAKE_INSTALL_LIBDIR})
        install(FILES "$<TARGET_FILE:${_target}>"
            DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RENAME "$<TARGET_SONAME_FILE_NAME:${_target}>")
    endforeach()

    # KernelDispatch/TaskExecutor.h includes <oneapi/tbb/...>; the product must carry that root.
    if(DEFINED TBB_SOURCE_DIR AND EXISTS "${TBB_SOURCE_DIR}/include/oneapi/tbb.h")
        install(DIRECTORY "${TBB_SOURCE_DIR}/include/"
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.inl")
    else()
        message(FATAL_ERROR "VIXEN native product install: the oneTBB headers are missing "
            "(TBB_SOURCE_DIR='${TBB_SOURCE_DIR}'); KernelDispatch/TaskExecutor.h includes "
            "<oneapi/tbb/...>. A system TBB (find_package) is not "
            "product-owned; configure so VIXEN/dependencies/native fetches oneTBB, which sets "
            "TBB_SOURCE_DIR.")
    endif()

    install(DIRECTORY "${VIXEN_ROOT}/libraries/KernelDispatch/include/"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.inl")

    if(DEFINED gaia_SOURCE_DIR AND EXISTS "${gaia_SOURCE_DIR}/single_include")
        install(DIRECTORY "${gaia_SOURCE_DIR}/single_include/"
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.inl")
    endif()

    if(DEFINED glm_SOURCE_DIR AND EXISTS "${glm_SOURCE_DIR}/glm")
        install(DIRECTORY "${glm_SOURCE_DIR}/glm"
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.inl")
    endif()

    if(DEFINED nlohmann_json_SOURCE_DIR AND EXISTS "${nlohmann_json_SOURCE_DIR}/include")
        install(DIRECTORY "${nlohmann_json_SOURCE_DIR}/include/"
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.inl")
    endif()
endfunction()
