
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
    $<TARGET_OBJECTS:lua51-core>
    $<TARGET_OBJECTS:lua51-std>
    ${LUA_PRODUCT_RESOURCE}
)
target_link_libraries(lumenc PRIVATE ${LUA_LINK_LIBS})
target_compile_definitions(lumenc PRIVATE ${LUA_COMPILE_DEF})
