
add_executable(HelloCXX hello_cxx.cpp)
target_link_libraries(HelloCXX PRIVATE Lumen)
add_custom_command(
    TARGET HelloCXX POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Lumen> $<TARGET_FILE_DIR:HelloCXX>
)