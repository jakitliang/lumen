add_library(
    lumen-lua OBJECT
    base.cpp
    bit.cpp
    c_api.cpp
    c_aux.cpp
    debug.cpp
    init.cpp
    io.cpp
    load.cpp
    load_platform.c
    math.cpp
    os.cpp
    string.cpp
    table.cpp
    utf8lib.cpp
)
if (LUA_BUILD_TYPE MATCHES "SHARED")
    if (CMAKE_SYSTEM_NAME MATCHES "Windows")
        list(APPEND LUA_COMPILE_DEF LUA_BUILD_AS_DLL)
        set(LUA_PRODUCT_RESOURCE ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
        configure_file(
            version.rc.in
            "${LUA_PRODUCT_RESOURCE}"
            @ONLY
        )
    else ()
        set_target_properties(lumen-lua PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
    endif ()
endif ()
target_compile_definitions(lumen-lua PUBLIC ${LUA_COMPILE_DEF})
target_compile_options(lumen-lua PUBLIC ${LUA_COMPILE_OPT})
target_link_libraries(lumen-lua PUBLIC ${LUA_LINK_LIBS})

# ---------------------------------------------------

add_library(
    lumen-shared SHARED
    ${LUA_PRODUCT_RESOURCE}
    $<TARGET_OBJECTS:lumen-core>
    $<TARGET_OBJECTS:lumen-lua>
)
target_include_directories(
    lumen-shared INTERFACE
    $<BUILD_INTERFACE:${LUA_BASE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_definitions(lumen-shared INTERFACE ${LUA_COMPILE_DEF})
target_compile_options(lumen-shared INTERFACE ${LUA_COMPILE_OPT})
target_link_libraries(lumen-shared INTERFACE ${LUA_LINK_LIBS})
set_target_properties(
    lumen-shared PROPERTIES OUTPUT_NAME "Lumen"
)

# ---------------------------------------------------

add_library(
    lumen-static STATIC
    ${LUA_PRODUCT_RESOURCE}
    $<TARGET_OBJECTS:lumen-core>
    $<TARGET_OBJECTS:lumen-lua>
)
target_include_directories(
    lumen-static INTERFACE
    $<BUILD_INTERFACE:${LUA_BASE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_definitions(lumen-static INTERFACE ${LUA_COMPILE_DEF})
target_compile_options(lumen-static INTERFACE ${LUA_COMPILE_OPT})
target_link_libraries(lumen-static INTERFACE ${LUA_LINK_LIBS})

# ---------------------------------------------------

if (LUA_BUILD_TYPE MATCHES "SHARED")
    add_library(Lumen ALIAS lumen-shared)
else ()
    add_library(Lumen ALIAS lumen-static)
endif ()

if (NOT LUA_SKIP_INSTALL)
    install(
        FILES
        ${LUMEN_BASE_SOURCE_DIR}/include/lauxlib.h
        ${LUMEN_BASE_SOURCE_DIR}/include/lua.h
        ${LUMEN_BASE_SOURCE_DIR}/include/lua.hpp
        ${LUMEN_BASE_SOURCE_DIR}/include/luaconf.h
        ${LUMEN_BASE_SOURCE_DIR}/include/lualib.h
#        ${LUMEN_BASE_SOURCE_DIR}/include/lumen.h
        DESTINATION include
    )
    install(
        TARGETS lumen-shared
        EXPORT lumen-shared
        INCLUDES DESTINATION include
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
    )
    install(
        EXPORT lumen-shared
        FILE lumen-sharedConfig.cmake
        DESTINATION lib/cmake/Lumen
    )
    install(
        FILES
        ${LUMEN_BASE_SOURCE_DIR}/etc/lumen.pc
        DESTINATION lib/pkgconfig
    )
endif ()
