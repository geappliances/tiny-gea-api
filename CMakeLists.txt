add_library(tiny_gea3 INTERFACE)

target_sources(tiny_gea3 INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/src/tiny_erd_client.c
  ${CMAKE_CURRENT_LIST_DIR}/src/tiny_gea3_interface.c
)

target_include_directories(tiny_gea3 INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)

target_link_libraries(tiny_gea3 INTERFACE
  tiny
)
