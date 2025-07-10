
if (LUA_USE_INTERPRETER)
    add_subdirectory(light)
endif ()

if (LUA_USE_COMPILER)
    add_subdirectory(lightc)
endif ()
