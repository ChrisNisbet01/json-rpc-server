set(APP rpc_server)
set(CMAKE_C_FLAGS "-Wall -Wextra -Werror")

find_library(UBOX ubox)

# Generate the grammar library using the macro
epc_generate_grammar(
    TARGET json_grammar
    GDL_FILE ${CMAKE_CURRENT_SOURCE_DIR}/json.gdl
    BOOTSTRAP_AST
)

set(SOURCE_FILES 
  main.c
  server.c
  utils.c
  json_ast_actions.c
)

add_compile_definitions(_GNU_SOURCE)
add_executable(${APP} ${SOURCE_FILES})
add_dependencies(${APP} easy_pc_static json_grammar)


target_link_libraries(${APP}
  PRIVATE 
    json_grammar
    easy_pc_static
    PkgConfig::JSON_C
    ${UBOX}
)

target_compile_features(${APP} PRIVATE c_std_11)
