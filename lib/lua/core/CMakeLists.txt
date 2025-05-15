
if (LUA_USE_CORE_DEBUG)
    list(APPEND LUA_COMPILE_DEF LUA_CORE_DEBUG)
endif ()

add_library(
    lua51-core OBJECT
    api.cpp
    code.cpp
    debug.cpp
    do.cpp
    dump.cpp
    func.cpp
    gc.cpp
    lex.cpp
    mem.cpp
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
)
if (LUA_BUILD_TYPE MATCHES "SHARED")
    if (CMAKE_SYSTEM_NAME MATCHES "Windows")
        list(APPEND LUA_COMPILE_DEF LUA_BUILD_AS_DLL)
    else ()
        set_target_properties(lua51-core PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
    endif ()
endif ()
target_compile_definitions(lua51-core PRIVATE ${LUA_COMPILE_DEF})
target_compile_options(lua51-core PRIVATE ${LUA_COMPILE_OPT})
target_link_libraries(lua51-core PRIVATE ${LUA_LINK_LIBS})
