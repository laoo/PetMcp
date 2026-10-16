add_executable( pet_cli src/Main.cpp )

set_target_properties( pet_cli PROPERTIES OUTPUT_NAME petmcp-cli )

target_link_libraries( pet_cli
  PRIVATE pet::server CLI11::CLI11 pet::warnings )
