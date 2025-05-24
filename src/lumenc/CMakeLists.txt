
if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    set(LUA_PRODUCT_RESOURCE ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
    configure_file(
        version.rc.in
        "${LUA_PRODUCT_RESOURCE}"
        @ONLY
    )
endif ()

add_executable(
    lumenc
    lumenc.cpp
    print.cpp
    $<TARGET_OBJECTS:lumen-core>
    $<TARGET_OBJECTS:lumen-lua>
    ${LUA_PRODUCT_RESOURCE}
)
target_compile_definitions(lumenc PRIVATE ${LUA_COMPILE_DEF})
target_compile_options(lumenc INTERFACE ${LUA_COMPILE_OPT})
target_link_libraries(lumenc PRIVATE ${LUA_LINK_LIBS})

if (NOT LUA_SKIP_INSTALL)
    install(
        TARGETS lumenc
        DESTINATION bin
    )
    install(
        FILES
        ${LUMEN_BASE_SOURCE_DIR}/doc/lumenc.1
        DESTINATION share/man/man1
    )
endif ()