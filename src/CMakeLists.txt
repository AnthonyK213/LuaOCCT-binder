aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} LUAOCCT_GEN_SRCS)

add_executable(
  luaocct-binder
  ${LUAOCCT_GEN_SRCS}
  )

target_link_libraries(
  luaocct-binder
  PRIVATE
  ${LIBCLANG_LIBRARIES}
  )

if(WIN32)
  add_custom_command(
    TARGET luaocct-binder POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "$ENV{LIBCLANG_DIR}/bin/libclang.dll"
        "$<TARGET_PROPERTY:luaocct-binder,BINARY_DIR>/$<CONFIG>"
    COMMAND ${CMAKE_COMMAND} -E copy
        "$ENV{LIBCLANG_DIR}/bin/vcruntime140.dll"
        "$<TARGET_PROPERTY:luaocct-binder,BINARY_DIR>/$<CONFIG>"
    COMMAND ${CMAKE_COMMAND} -E copy
        "$ENV{LIBCLANG_DIR}/bin/vcruntime140_1.dll"
        "$<TARGET_PROPERTY:luaocct-binder,BINARY_DIR>/$<CONFIG>"
    )
endif()
