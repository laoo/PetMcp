add_library( pet_server
  src/JsonLinesServer.cpp
  src/McpHttpServer.cpp
  src/McpServer.cpp
  src/TcpLineServer.cpp )

add_library( pet::server ALIAS pet_server )

target_include_directories( pet_server PUBLIC include )

target_link_libraries( pet_server
  PUBLIC  pet::core
  PRIVATE pet::warnings httplib Threads::Threads $<$<PLATFORM_ID:Windows>:ws2_32> )
