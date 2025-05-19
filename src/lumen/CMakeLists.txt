if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    set(LUA_PRODUCT_RESOURCE ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
    configure_file(
        version.rc.in
        "${LUA_PRODUCT_RESOURCE}"
        @ONLY
    )
endif ()

add_executable(lumen lumen.c ${LUA_PRODUCT_RESOURCE} ${LUA_PRODUCT_RESOURCE})
target_compile_definitions(lumen PRIVATE ${LUA_COMPILE_DEF})
target_compile_options(lumen INTERFACE ${LUA_COMPILE_OPT})
target_link_libraries(lumen PRIVATE Lumen ${LUA_LINK_LIBS})

if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    add_custom_command(
        TARGET lumen POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Lumen> $<TARGET_FILE_DIR:lumen>
    )
endif ()

if (NOT LUA_SKIP_INSTALL)
    install(
        TARGETS lumen
        DESTINATION bin
    )
    if (CMAKE_SYSTEM_NAME MATCHES "Darwin")
        install(CODE "
            execute_process(
                COMMAND install_name_tool -add_rpath \"@executable_path/../lib\" \"\${CMAKE_INSTALL_PREFIX}/bin/lumen\"
            )
        ")
    elseif (CMAKE_SYSTEM_NAME MATCHES "Linux" OR CMAKE_SYSTEM_NAME MATCHES "Android")
        install(CODE "
            execute_process(
                COMMAND patchelf --set-rpath \"\\\$ORIGIN/../lib\" \"\${CMAKE_INSTALL_PREFIX}/bin/lumen\"
                RESULT_VARIABLE patch_result
            )
            if(NOT \${patch_result} EQUAL 0)
                message(WARNING \"patchelf command failed, RPATH might not be set correctly\")
            endif()
        ")
    endif ()
    install(
        FILES
        ${LUMEN_BASE_SOURCE_DIR}/doc/lumen.1
        DESTINATION share/man/man1
    )
endif ()