
if (LUA_USE_CORE_DEBUG)
    list(APPEND LUA_COMPILE_DEF LUA_CORE_DEBUG)
endif ()

add_library(
    lumen-core OBJECT
    api.cpp
    buffer.cpp
    code.cpp
    debug.cpp
    do.cpp
    dump.cpp
    func.cpp
    gc.cpp
    i_state.cpp
    lex.cpp
    lumen.cpp
    memory.cpp
    object.cpp
    opcodes.cpp
    parser.cpp
    state.cpp
    string.cpp
    table.cpp
    tm.cpp
    undump.cpp
    vm.cpp
    zio.cpp
    protected_call.cpp
)
if (LUA_BUILD_TYPE MATCHES "SHARED")
    if (CMAKE_SYSTEM_NAME MATCHES "Windows")
        list(APPEND LUA_COMPILE_DEF LUA_BUILD_AS_DLL)
    else ()
        set_target_properties(lumen-core PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
    endif ()
endif ()
target_compile_definitions(lumen-core PRIVATE ${LUA_COMPILE_DEF})
target_compile_options(lumen-core PRIVATE ${LUA_COMPILE_OPT})
target_link_libraries(lumen-core PRIVATE ${LUA_LINK_LIBS})

add_library(
    lumen-core-header INTERFACE
)
target_include_directories(
    lumen-core-header INTERFACE
    $<BUILD_INTERFACE:${LUMEN_BASE_SOURCE_DIR}/internal>
    $<INSTALL_INTERFACE:include>
)
