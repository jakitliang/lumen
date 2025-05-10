
add_library(
    lua51-std OBJECT
    lauxlib.c
    lbaselib.c
    ldblib.c
    linit.c
    liolib.c
    lmathlib.c
    loadlib.c
    loslib.c
    lstrlib.c
    ltablib.c
)
if (LUA_BUILD_TYPE MATCHES "SHARED")
    if (CMAKE_SYSTEM_NAME MATCHES "Windows")
        list(APPEND LUA_COMPILE_DEF LUA_BUILD_AS_DLL)
    else ()
        set_target_properties(lua51-std PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
    endif ()
endif ()
target_compile_definitions(lua51-std PRIVATE ${LUA_COMPILE_DEF})
target_compile_options(lua51-std PRIVATE ${LUA_COMPILE_OPT})
target_link_libraries(lua51-std PRIVATE ${LUA_LINK_LIBS})
