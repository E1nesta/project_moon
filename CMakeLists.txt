cmake_minimum_required(VERSION 3.16)

project(mobile_game_backend VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
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
    common/src/config/simple_config.cpp
    common/src/log/logger.cpp
    common/src/mysql/mysql_client.cpp
    common/src/mysql/mysql_client_pool.cpp
    common/src/net/packet.cpp
    common/src/net/proto_mapper.cpp
    common/src/redis/redis_client.cpp
    common/src/redis/redis_client_pool.cpp
    common/src/security/password_hasher.cpp
)

target_include_directories(common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/common/include
        ${CMAKE_CURRENT_BINARY_DIR}
        ${MYSQLCLIENT_INCLUDE_DIRS}
        ${HIREDIS_INCLUDE_DIRS}
        ${OPENSSL_INCLUDE_DIRS}
        ${Boost_INCLUDE_DIRS}
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

target_compile_definitions(common PUBLIC MOBILE_GAME_BACKEND_VERSION="${PROJECT_VERSION}")

add_library(framework STATIC
    framework/execution/execution_types.cpp
    framework/execution/sharded_request_executor.cpp
    framework/protocol/context_enrichment.cpp
    framework/runtime/service_options.cpp
    framework/protocol/context_extractor.cpp
    framework/protocol/error_responder.cpp
    framework/protocol/message_policy_registry.cpp
    framework/protocol/packet_codec.cpp
    framework/protocol/request_dispatcher.cpp
    framework/transport/transport_server.cpp
    framework/transport/transport_client.cpp
    framework/service/service_app.cpp
)

target_include_directories(framework
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/common/include
        ${CMAKE_CURRENT_BINARY_DIR}
        ${Boost_INCLUDE_DIRS}
)

target_link_libraries(framework
    PUBLIC
        common
        Boost::system
        Threads::Threads
)

if(MSVC)
    target_compile_options(common PRIVATE /W4 /permissive-)
    target_compile_options(framework PRIVATE /W4 /permissive-)
else()
    target_compile_options(common PRIVATE -Wall -Wextra -Wpedantic ${MYSQLCLIENT_CFLAGS_OTHER} ${HIREDIS_CFLAGS_OTHER} ${OPENSSL_CFLAGS_OTHER})
    target_compile_options(framework PRIVATE -Wall -Wextra -Wpedantic)
endif()

function(add_backend_executable target)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE framework)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_backend_executable(gateway_server
    services/gateway/main.cpp
    services/gateway/gateway_forward_executor.cpp
    services/gateway/gateway_server_app.cpp
    services/gateway/session_binding_store.cpp
    login_server/session/redis_session_repository.cpp
)

add_backend_executable(login_server
    services/login/main.cpp
    services/login/login_server_app.cpp
    login_server/login_service.cpp
    login_server/auth/mysql_account_repository.cpp
    login_server/session/redis_session_repository.cpp
)

add_backend_executable(player_server
    services/player/main.cpp
    services/player/player_server_app.cpp
    game_server/player/player_service.cpp
    game_server/player/mysql_player_repository.cpp
    game_server/player/redis_player_cache_repository.cpp
    login_server/session/redis_session_repository.cpp
)

add_backend_executable(dungeon_server
    services/dungeon/main.cpp
    services/dungeon/dungeon_server_app.cpp
    dungeon_server/dungeon/dungeon_service.cpp
    dungeon_server/dungeon/in_memory_dungeon_config_repository.cpp
    dungeon_server/dungeon/mysql_dungeon_repository.cpp
    dungeon_server/dungeon/redis_player_lock_repository.cpp
    dungeon_server/dungeon/redis_battle_context_repository.cpp
    game_server/player/mysql_player_repository.cpp
    game_server/player/redis_player_cache_repository.cpp
    login_server/session/redis_session_repository.cpp
)

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
target_link_libraries(demo_client PRIVATE framework)
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
    target_compile_options(demo_flow PRIVATE /W4 /permissive-)
    target_compile_options(demo_client PRIVATE /W4 /permissive-)
    target_compile_options(service_check PRIVATE /W4 /permissive-)
    target_compile_options(password_tool PRIVATE /W4 /permissive-)
else()
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

add_executable(session_binding_store_test
    tests/session_binding_store_test.cpp
    services/gateway/session_binding_store.cpp
    login_server/session/in_memory_session_repository.cpp
)
target_link_libraries(session_binding_store_test PRIVATE common)
target_include_directories(session_binding_store_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(packet_codec_test
    tests/packet_codec_test.cpp
    framework/protocol/packet_codec.cpp
)
target_link_libraries(packet_codec_test PRIVATE common)
target_include_directories(packet_codec_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(request_dispatcher_test
    tests/request_dispatcher_test.cpp
    framework/protocol/request_dispatcher.cpp
    framework/protocol/error_responder.cpp
)
target_link_libraries(request_dispatcher_test PRIVATE common)
target_include_directories(request_dispatcher_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    target_compile_options(login_service_test PRIVATE /W4 /permissive-)
    target_compile_options(player_service_test PRIVATE /W4 /permissive-)
    target_compile_options(dungeon_service_test PRIVATE /W4 /permissive-)
    target_compile_options(session_binding_store_test PRIVATE /W4 /permissive-)
    target_compile_options(packet_codec_test PRIVATE /W4 /permissive-)
    target_compile_options(request_dispatcher_test PRIVATE /W4 /permissive-)
else()
    target_compile_options(login_service_test PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(player_service_test PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(dungeon_service_test PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(session_binding_store_test PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(packet_codec_test PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(request_dispatcher_test PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME login_service_test COMMAND login_service_test)
add_test(NAME player_service_test COMMAND player_service_test)
add_test(NAME dungeon_service_test COMMAND dungeon_service_test)
add_test(NAME session_binding_store_test COMMAND session_binding_store_test)
add_test(NAME packet_codec_test COMMAND packet_codec_test)
add_test(NAME request_dispatcher_test COMMAND request_dispatcher_test)
