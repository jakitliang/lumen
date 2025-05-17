
add_subdirectory(core)

add_subdirectory(std)

if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    set(LUA_PRODUCT_RESOURCE ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
    configure_file(
        version.rc.in
        "${LUA_PRODUCT_RESOURCE}"
        @ONLY
    )
endif ()

add_library(
    lua51-shared SHARED
    ${LUA_PRODUCT_RESOURCE}
    $<TARGET_OBJECTS:lua51-core>
    $<TARGET_OBJECTS:lua51-std>
    $<TARGET_OBJECTS:Lumen>
)
set_target_properties(
    lua51-shared PROPERTIES OUTPUT_NAME "lua51"
)
target_include_directories(
    lua51-shared INTERFACE
    $<BUILD_INTERFACE:${LUA_BASE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_definitions(lua51-shared INTERFACE ${LUA_COMPILE_DEF})
target_compile_options(lua51-shared INTERFACE ${LUA_COMPILE_OPT})
target_link_libraries(lua51-shared INTERFACE ${LUA_LINK_LIBS})

add_library(
    lua51-static STATIC
    ${LUA_PRODUCT_RESOURCE}
    $<TARGET_OBJECTS:lua51-core>
    $<TARGET_OBJECTS:lua51-std>
    $<TARGET_OBJECTS:Lumen>
)
target_include_directories(
    lua51-static INTERFACE
    $<BUILD_INTERFACE:${LUA_BASE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_definitions(lua51-static INTERFACE ${LUA_COMPILE_DEF})
target_compile_options(lua51-static INTERFACE ${LUA_COMPILE_OPT})
target_link_libraries(lua51-static INTERFACE ${LUA_LINK_LIBS})

if (LUA_BUILD_TYPE MATCHES "SHARED")
    add_library(lua51 ALIAS lua51-shared)
else ()
    add_library(lua51 ALIAS lua51-static)
endif ()
