set(APP rpc_server)
set(CMAKE_C_FLAGS "-Wall -Wextra -Werror")

find_library(UBOX ubox)

set(SOURCE_FILES
  main.c
  server.c
  handlers.c
  framing.c
  rpc.c
  transport.c
  utils.c
)

add_compile_definitions(_GNU_SOURCE)
add_executable(${APP} ${SOURCE_FILES})

target_link_libraries(${APP}
  PRIVATE
    PkgConfig::JSON_C
    ${UBOX}
)

target_compile_features(${APP} PRIVATE c_std_11)
