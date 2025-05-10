
if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    set(LUA_PRODUCT_RESOURCE ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
    configure_file(
        version.rc.in
        "${LUA_PRODUCT_RESOURCE}"
        @ONLY
    )
endif ()

add_executable(lumen lumen.c ${LUA_PRODUCT_RESOURCE} ${LUA_PRODUCT_RESOURCE})
target_link_libraries(lumen PRIVATE lua51 ${LUA_LINK_LIBS})
target_compile_definitions(lumen PRIVATE ${LUA_COMPILE_DEF})

if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    add_custom_command(
        TARGET lumen POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:lua51>
        $<TARGET_FILE_DIR:lumen>
    )
endif ()
