add_library(
    lumen-lua OBJECT
    auxiliary.cpp
    base.cpp
    bit.cpp
    debug.cpp
    init.cpp
    io.cpp
    load.cpp
    load_platform.c
    math.cpp
    object.cpp
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
target_compile_definitions(lumen-lua PRIVATE ${LUA_COMPILE_DEF})
target_compile_options(lumen-lua PRIVATE ${LUA_COMPILE_OPT})
target_link_libraries(lumen-lua PRIVATE ${LUA_LINK_LIBS})

# ----- Lumen Header -----

add_library(
    lumen-header INTERFACE
)
target_include_directories(
    lumen-header INTERFACE
    $<BUILD_INTERFACE:${LUMEN_BASE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# ----- Lumen Links -----

add_library(
    lumen-links INTERFACE
)
target_compile_definitions(lumen-links INTERFACE ${LUA_COMPILE_DEF})
target_compile_options(lumen-links INTERFACE ${LUA_COMPILE_OPT})
target_link_libraries(lumen-links INTERFACE ${LUA_LINK_LIBS})

# ----- Lumen Shared Library -----

if (NOT DEFINED LUA_BUILD_STATIC_ONLY)
    add_library(
        lumen-shared SHARED EXCLUDE_FROM_ALL
        ${LUA_PRODUCT_RESOURCE}
        $<TARGET_OBJECTS:lumen-core>
        $<TARGET_OBJECTS:lumen-lua>
    )
    target_include_directories(
        lumen-shared INTERFACE
        $<BUILD_INTERFACE:${LUMEN_BASE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    target_compile_definitions(lumen-shared INTERFACE ${LUA_COMPILE_DEF})
    target_compile_options(lumen-shared INTERFACE ${LUA_COMPILE_OPT})
    target_link_libraries(lumen-shared INTERFACE ${LUA_LINK_LIBS})
    set_target_properties(
        lumen-shared PROPERTIES OUTPUT_NAME ${LUA_SHARED_LIB_NAME}
    )
endif ()


# ----- Lumen Static Library -----

add_library(
    lumen-static STATIC
    ${LUA_PRODUCT_RESOURCE}
    $<TARGET_OBJECTS:lumen-core>
    $<TARGET_OBJECTS:lumen-lua>
)
target_include_directories(
    lumen-static INTERFACE
    $<BUILD_INTERFACE:${LUMEN_BASE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_definitions(lumen-static INTERFACE ${LUA_COMPILE_DEF})
target_compile_options(lumen-static INTERFACE ${LUA_COMPILE_OPT})
target_link_libraries(lumen-static INTERFACE ${LUA_LINK_LIBS})

# ----- Lumen Install -----

if (LUA_BUILD_TYPE MATCHES "SHARED" AND NOT DEFINED LUA_BUILD_STATIC_ONLY)
    add_library(Lumen ALIAS lumen-shared)
    if (LUA_USE_COMPAT)
        add_library(lua51 ALIAS lumen-shared)
    endif ()
else ()
    add_library(Lumen ALIAS lumen-static)
    if (LUA_USE_COMPAT)
        add_library(lua51 ALIAS lumen-static)
    endif ()
endif ()

if (NOT LUA_SKIP_INSTALL)
    install(
        FILES
        ${LUMEN_BASE_SOURCE_DIR}/include/lauxlib.h
        ${LUMEN_BASE_SOURCE_DIR}/include/lua.h
        ${LUMEN_BASE_SOURCE_DIR}/include/luaconf.h
        ${LUMEN_BASE_SOURCE_DIR}/include/lualib.h
        ${LUMEN_BASE_SOURCE_DIR}/include/lumen.h
        DESTINATION include
    )
    if (NOT DEFINED LUA_BUILD_STATIC_ONLY)
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
            FILE ${LUA_SHARED_LIB_NAME}Config.cmake
            DESTINATION lib/cmake/${LUA_SHARED_LIB_NAME}
        )
    endif ()
    install(
        FILES
        ${LUMEN_BASE_SOURCE_DIR}/etc/lumen.pc
        DESTINATION lib/pkgconfig
    )
endif ()
