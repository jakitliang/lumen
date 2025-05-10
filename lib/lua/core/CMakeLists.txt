

add_library(
    lua51-core OBJECT
    lapi.cpp
    lcode.cpp
    ldebug.cpp
    ldo.cpp
    ldump.cpp
    lfunc.cpp
    lgc.cpp
    llex.cpp
    lmem.cpp
    lobject.cpp
    lopcodes.cpp
    lparser.cpp
    lstate.cpp
    lstring.cpp
    ltable.cpp
    ltm.cpp
    lundump.cpp
    lvm.cpp
    lzio.cpp
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
