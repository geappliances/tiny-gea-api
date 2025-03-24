add_library(tiny_gea_api INTERFACE)

target_sources(tiny_gea_api INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/src/tiny_gea2_erd_client.c
  ${CMAKE_CURRENT_LIST_DIR}/src/tiny_gea2_interface.c
  ${CMAKE_CURRENT_LIST_DIR}/src/tiny_gea3_erd_client.c
  ${CMAKE_CURRENT_LIST_DIR}/src/tiny_gea3_interface.c
)

target_include_directories(tiny_gea_api INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)

target_link_libraries(tiny_gea_api INTERFACE
  tiny
)
