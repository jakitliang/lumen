
if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    set(LUA_PRODUCT_RESOURCE ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
    configure_file(
        version.rc.in
        "${LUA_PRODUCT_RESOURCE}"
        @ONLY
    )
endif ()

add_executable(
    lightc
    lightc.cpp
    print.cpp
    $<TARGET_OBJECTS:lumen-core>
    $<TARGET_OBJECTS:lumen-lua>
    ${LUA_PRODUCT_RESOURCE}
)
target_compile_definitions(lightc PRIVATE ${LUA_COMPILE_DEF})
target_compile_options(lightc INTERFACE ${LUA_COMPILE_OPT})
target_link_libraries(lightc PRIVATE ${LUA_LINK_LIBS})

if (NOT LUA_SKIP_INSTALL)
    install(
        TARGETS lightc
        DESTINATION bin
    )
    install(
        FILES
        ${LUMEN_BASE_SOURCE_DIR}/doc/lightc.1
        DESTINATION share/man/man1
    )
endif ()