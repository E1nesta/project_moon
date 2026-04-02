cmake_minimum_required(VERSION 3.16)

project(mobile_game_backend VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
find_package(Protobuf REQUIRED)
pkg_check_modules(MYSQLCLIENT REQUIRED mysqlclient)
pkg_check_modules(HIREDIS REQUIRED hiredis)
pkg_check_modules(OPENSSL REQUIRED openssl)

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS proto/game_backend.proto)

add_library(protocol STATIC
    ${PROTO_SRCS}
)

target_link_libraries(protocol
    PUBLIC
        protobuf::libprotobuf
)

target_include_directories(protocol
    PUBLIC
        ${CMAKE_CURRENT_BINARY_DIR}
)

add_library(common STATIC
    common/src/bootstrap/service_app.cpp
    common/src/config/simple_config.cpp
    common/src/log/logger.cpp
    common/src/mysql/mysql_client.cpp
    common/src/net/packet.cpp
    common/src/net/proto_mapper.cpp
    common/src/net/tcp_client.cpp
    common/src/net/tcp_server.cpp
    common/src/redis/redis_client.cpp
    common/src/security/password_hasher.cpp
)

target_include_directories(common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/common/include
        ${CMAKE_CURRENT_BINARY_DIR}
        ${MYSQLCLIENT_INCLUDE_DIRS}
        ${HIREDIS_INCLUDE_DIRS}
        ${OPENSSL_INCLUDE_DIRS}
)

target_link_directories(common
    PUBLIC
        ${MYSQLCLIENT_LIBRARY_DIRS}
        ${HIREDIS_LIBRARY_DIRS}
        ${OPENSSL_LIBRARY_DIRS}
)

target_compile_features(common PUBLIC cxx_std_17)
target_link_libraries(common
    PUBLIC
        protocol
        ${MYSQLCLIENT_LIBRARIES}
        ${HIREDIS_LIBRARIES}
        ${OPENSSL_LIBRARIES}
)

if(MSVC)
    target_compile_options(common PRIVATE /W4 /permissive-)
else()
    target_compile_options(common PRIVATE -Wall -Wextra -Wpedantic ${MYSQLCLIENT_CFLAGS_OTHER} ${HIREDIS_CFLAGS_OTHER} ${OPENSSL_CFLAGS_OTHER})
endif()

target_compile_definitions(common PUBLIC MOBILE_GAME_BACKEND_VERSION="${PROJECT_VERSION}")

add_executable(gateway
    gateway/main.cpp
    gateway/gateway_server.cpp
    gateway/session_binding_authorizer.cpp
    login_server/session/redis_session_repository.cpp
)
target_link_libraries(gateway PRIVATE common)
target_include_directories(gateway PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(login_server
    login_server/main.cpp
    login_server/login_network_server.cpp
    login_server/login_service.cpp
    login_server/auth/mysql_account_repository.cpp
    login_server/session/redis_session_repository.cpp
)
target_link_libraries(login_server PRIVATE common)
target_include_directories(login_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(game_server
    game_server/main.cpp
    game_server/game_network_server.cpp
    game_server/player/player_service.cpp
    game_server/player/mysql_player_repository.cpp
    game_server/player/redis_player_cache_repository.cpp
    login_server/session/redis_session_repository.cpp
)
target_link_libraries(game_server PRIVATE common)
target_include_directories(game_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(dungeon_server
    dungeon_server/main.cpp
    dungeon_server/dungeon_network_server.cpp
    dungeon_server/dungeon/dungeon_service.cpp
    dungeon_server/dungeon/in_memory_dungeon_config_repository.cpp
    dungeon_server/dungeon/mysql_dungeon_repository.cpp
    dungeon_server/dungeon/redis_player_lock_repository.cpp
    dungeon_server/dungeon/redis_battle_context_repository.cpp
    game_server/player/mysql_player_repository.cpp
    game_server/player/redis_player_cache_repository.cpp
    login_server/session/redis_session_repository.cpp
)
target_link_libraries(dungeon_server PRIVATE common)
target_include_directories(dungeon_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(demo_flow
    tools/demo_flow/main.cpp
    login_server/login_service.cpp
    login_server/auth/in_memory_account_repository.cpp
    login_server/session/in_memory_session_repository.cpp
    game_server/player/player_service.cpp
    game_server/player/in_memory_player_repository.cpp
    dungeon_server/dungeon/dungeon_service.cpp
    login_server/auth/mysql_account_repository.cpp
    login_server/session/redis_session_repository.cpp
    game_server/player/mysql_player_repository.cpp
    game_server/player/redis_player_cache_repository.cpp
    dungeon_server/dungeon/mysql_dungeon_repository.cpp
    dungeon_server/dungeon/redis_player_lock_repository.cpp
    dungeon_server/dungeon/redis_battle_context_repository.cpp
    dungeon_server/dungeon/in_memory_dungeon_config_repository.cpp
)
target_link_libraries(demo_flow PRIVATE common)
target_include_directories(demo_flow PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(demo_client
    tools/demo_client/main.cpp
    login_server/auth/mysql_account_repository.cpp
)
target_link_libraries(demo_client PRIVATE common)
target_include_directories(demo_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(service_check
    tools/service_check/main.cpp
)
target_link_libraries(service_check PRIVATE common)
target_include_directories(service_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(password_tool
    tools/password_tool/main.cpp
)
target_link_libraries(password_tool PRIVATE common)
target_include_directories(password_tool PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    target_compile_options(gateway PRIVATE /W4 /permissive-)
    target_compile_options(login_server PRIVATE /W4 /permissive-)
    target_compile_options(game_server PRIVATE /W4 /permissive-)
    target_compile_options(dungeon_server PRIVATE /W4 /permissive-)
    target_compile_options(demo_flow PRIVATE /W4 /permissive-)
    target_compile_options(demo_client PRIVATE /W4 /permissive-)
    target_compile_options(service_check PRIVATE /W4 /permissive-)
    target_compile_options(password_tool PRIVATE /W4 /permissive-)
else()
    target_compile_options(gateway PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(login_server PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(game_server PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(dungeon_server PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(demo_flow PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(demo_client PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(service_check PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(password_tool PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(login_service_test
    tests/login_service_test.cpp
    login_server/login_service.cpp
    login_server/auth/in_memory_account_repository.cpp
    login_server/session/in_memory_session_repository.cpp
)
target_link_libraries(login_service_test PRIVATE common)
target_include_directories(login_service_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(player_service_test
    tests/player_service_test.cpp
    game_server/player/player_service.cpp
    game_server/player/in_memory_player_repository.cpp
    login_server/session/in_memory_session_repository.cpp
)
target_link_libraries(player_service_test PRIVATE common)
target_include_directories(player_service_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(dungeon_service_test
    tests/dungeon_service_test.cpp
    dungeon_server/dungeon/dungeon_service.cpp
    dungeon_server/dungeon/in_memory_dungeon_config_repository.cpp
    dungeon_server/dungeon/mysql_dungeon_repository.cpp
    login_server/session/in_memory_session_repository.cpp
)
target_link_libraries(dungeon_service_test PRIVATE common)
target_include_directories(dungeon_service_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(session_binding_authorizer_test
    tests/session_binding_authorizer_test.cpp
    gateway/session_binding_authorizer.cpp
    login_server/session/in_memory_session_repository.cpp
)
target_link_libraries(session_binding_authorizer_test PRIVATE common)
target_include_directories(session_binding_authorizer_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    target_compile_options(login_service_test PRIVATE /W4 /permissive-)
    target_compile_options(player_service_test PRIVATE /W4 /permissive-)
    target_compile_options(dungeon_service_test PRIVATE /W4 /permissive-)
    target_compile_options(session_binding_authorizer_test PRIVATE /W4 /permissive-)
else()
    target_compile_options(login_service_test PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(player_service_test PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(dungeon_service_test PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(session_binding_authorizer_test PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME login_service_test COMMAND login_service_test)
add_test(NAME player_service_test COMMAND player_service_test)
add_test(NAME dungeon_service_test COMMAND dungeon_service_test)
add_test(NAME session_binding_authorizer_test COMMAND session_binding_authorizer_test)
