
add_library(
    Lumen-objs OBJECT
    memory.cpp
    auxlib.cpp
    baselib.c
    dblib.c
    init.c
    iolib.c
    mathlib.c
    loadlib.c
    oslib.c
    strlib.cpp
    tablib.c
    bitlib.c
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
        set_target_properties(Lumen-objs PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
    endif ()
endif ()
target_compile_definitions(Lumen-objs PRIVATE ${LUA_COMPILE_DEF})
target_compile_options(Lumen-objs PRIVATE ${LUA_COMPILE_OPT})
target_link_libraries(Lumen-objs PRIVATE ${LUA_LINK_LIBS})

# ---------------------------------------------------

add_library(
    Lumen-shared SHARED
    ${LUA_PRODUCT_RESOURCE}
    $<TARGET_OBJECTS:lua>
    $<TARGET_OBJECTS:Lumen-objs>
)
target_include_directories(
    Lumen-shared INTERFACE
    $<BUILD_INTERFACE:${LUA_BASE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_definitions(Lumen-shared INTERFACE ${LUA_COMPILE_DEF})
target_compile_options(Lumen-shared INTERFACE ${LUA_COMPILE_OPT})
target_link_libraries(Lumen-shared INTERFACE ${LUA_LINK_LIBS})
set_target_properties(
    Lumen-shared PROPERTIES OUTPUT_NAME "Lumen"
)

# ---------------------------------------------------

add_library(
    Lumen-static STATIC
    ${LUA_PRODUCT_RESOURCE}
    $<TARGET_OBJECTS:lua>
    $<TARGET_OBJECTS:Lumen-objs>
)
target_include_directories(
    Lumen-static INTERFACE
    $<BUILD_INTERFACE:${LUA_BASE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_definitions(Lumen-static INTERFACE ${LUA_COMPILE_DEF})
target_compile_options(Lumen-static INTERFACE ${LUA_COMPILE_OPT})
target_link_libraries(Lumen-static INTERFACE ${LUA_LINK_LIBS})

# ---------------------------------------------------

if (LUA_BUILD_TYPE MATCHES "SHARED")
    add_library(Lumen ALIAS Lumen-shared)
else ()
    add_library(Lumen ALIAS Lumen-static)
endif ()
