cmake_minimum_required(VERSION 3.16)

project(mobile_game_backend VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(spdlog REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Protobuf REQUIRED)
pkg_check_modules(MYSQLCLIENT REQUIRED mysqlclient)
pkg_check_modules(HIREDIS REQUIRED hiredis)
pkg_check_modules(OPENSSL REQUIRED openssl)

if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
endif()

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

function(set_backend_target_defaults target)
    target_include_directories(${target}
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}
            ${MYSQLCLIENT_INCLUDE_DIRS}
            ${HIREDIS_INCLUDE_DIRS}
            ${OPENSSL_INCLUDE_DIRS}
            ${Boost_INCLUDE_DIRS}
    )
    target_link_directories(${target}
        PUBLIC
            ${MYSQLCLIENT_LIBRARY_DIRS}
            ${HIREDIS_LIBRARY_DIRS}
            ${OPENSSL_LIBRARY_DIRS}
    )
    target_compile_features(${target} PUBLIC cxx_std_17)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic ${MYSQLCLIENT_CFLAGS_OTHER} ${HIREDIS_CFLAGS_OTHER} ${OPENSSL_CFLAGS_OTHER})
    endif()
endfunction()

add_library(runtime_foundation STATIC
    runtime/foundation/config/simple_config.cpp
    runtime/foundation/log/logger.cpp
    runtime/foundation/security/password_hasher.cpp
    runtime/foundation/security/session_token.cpp
)
set_backend_target_defaults(runtime_foundation)
target_link_libraries(runtime_foundation
    PUBLIC
        ${OPENSSL_LIBRARIES}
        spdlog::spdlog
)
target_compile_definitions(runtime_foundation PUBLIC MOBILE_GAME_BACKEND_VERSION="${PROJECT_VERSION}")

add_library(runtime_storage STATIC
    runtime/storage/mysql/mysql_client.cpp
    runtime/storage/mysql/mysql_client_pool.cpp
    runtime/storage/redis/redis_client.cpp
    runtime/storage/redis/redis_client_pool.cpp
)
set_backend_target_defaults(runtime_storage)
target_link_libraries(runtime_storage
    PUBLIC
        runtime_foundation
        ${MYSQLCLIENT_LIBRARIES}
        ${HIREDIS_LIBRARIES}
)

add_library(runtime_session STATIC
    runtime/session/in_memory_session_store.cpp
    runtime/session/redis_session_store.cpp
)
set_backend_target_defaults(runtime_session)
target_link_libraries(runtime_session
    PUBLIC
        runtime_foundation
        runtime_storage
)

add_library(runtime_protocol STATIC
    runtime/protocol/packet.cpp
    runtime/protocol/proto_mapper.cpp
    runtime/protocol/context_enrichment.cpp
    runtime/protocol/context_extractor.cpp
    runtime/protocol/error_responder.cpp
    runtime/protocol/message_policy_registry.cpp
    runtime/protocol/packet_codec.cpp
    runtime/protocol/request_dispatcher.cpp
)
set_backend_target_defaults(runtime_protocol)
target_link_libraries(runtime_protocol
    PUBLIC
        protocol
        runtime_foundation
        runtime_session
)

add_library(runtime_observability STATIC
    runtime/observability/structured_log.cpp
)
set_backend_target_defaults(runtime_observability)
target_link_libraries(runtime_observability
    PUBLIC
        runtime_foundation
        runtime_protocol
)

add_library(runtime_execution STATIC
    runtime/execution/execution_types.cpp
    runtime/execution/sharded_request_executor.cpp
)
set_backend_target_defaults(runtime_execution)
target_link_libraries(runtime_execution
    PUBLIC
        runtime_foundation
        runtime_observability
        runtime_protocol
)

add_library(runtime_transport STATIC
    runtime/transport/service_options.cpp
    runtime/transport/service_app.cpp
    runtime/transport/transport_server.cpp
    runtime/transport/transport_client.cpp
    runtime/transport/tls_options.cpp
)
set_backend_target_defaults(runtime_transport)
target_link_libraries(runtime_transport
    PUBLIC
        runtime_foundation
        runtime_observability
        runtime_protocol
        runtime_execution
        runtime_session
        runtime_storage
        Boost::system
        Threads::Threads
        ${OPENSSL_LIBRARIES}
)

add_library(runtime_core INTERFACE)
target_link_libraries(runtime_core
    INTERFACE
        runtime_foundation
        runtime_storage
        runtime_session
        runtime_observability
        runtime_protocol
        runtime_execution
        runtime_transport
)
target_compile_definitions(runtime_core INTERFACE MOBILE_GAME_BACKEND_VERSION="${PROJECT_VERSION}")

add_library(login_application STATIC
    modules/login/application/login_service.cpp
)
set_backend_target_defaults(login_application)
target_link_libraries(login_application
    PUBLIC
        runtime_foundation
        runtime_session
)

add_library(login_infrastructure STATIC
    modules/login/infrastructure/in_memory_account_repository.cpp
    modules/login/infrastructure/mysql_account_repository.cpp
)
set_backend_target_defaults(login_infrastructure)
target_link_libraries(login_infrastructure
    PUBLIC
        login_application
        runtime_foundation
        runtime_storage
)

add_library(player_application STATIC
    modules/player/application/player_service.cpp
)
set_backend_target_defaults(player_application)
target_link_libraries(player_application
    PUBLIC
        runtime_foundation
)

add_library(player_infrastructure STATIC
    modules/player/infrastructure/in_memory_player_repository.cpp
    modules/player/infrastructure/mysql_player_repository.cpp
    modules/player/infrastructure/redis_player_cache_repository.cpp
)
set_backend_target_defaults(player_infrastructure)
target_link_libraries(player_infrastructure
    PUBLIC
        player_application
        runtime_foundation
        runtime_storage
)

add_library(dungeon_application STATIC
    modules/dungeon/application/dungeon_service.cpp
)
set_backend_target_defaults(dungeon_application)
target_link_libraries(dungeon_application
    PUBLIC
        runtime_foundation
)

add_library(dungeon_infrastructure STATIC
    modules/dungeon/infrastructure/in_memory_dungeon_config_repository.cpp
    modules/dungeon/infrastructure/mysql_dungeon_repository.cpp
    modules/dungeon/infrastructure/redis_player_lock_repository.cpp
    modules/dungeon/infrastructure/redis_battle_context_repository.cpp
)
set_backend_target_defaults(dungeon_infrastructure)
target_link_libraries(dungeon_infrastructure
    PUBLIC
        dungeon_application
        runtime_foundation
        runtime_storage
)

add_library(gateway_support STATIC
    apps/gateway/forward_executor.cpp
    apps/gateway/rate_limit_service.cpp
    apps/gateway/request_router.cpp
    apps/gateway/session_binding_service.cpp
    apps/gateway/upstream_response_validator.cpp
)
set_backend_target_defaults(gateway_support)
target_link_libraries(gateway_support PUBLIC runtime_core)

function(add_backend_executable target)
    add_executable(${target} ${ARGN})
    set_backend_target_defaults(${target})
endfunction()

add_backend_executable(gateway_server
    apps/gateway/main.cpp
    apps/gateway/gateway_server_app.cpp
)
target_link_libraries(gateway_server PRIVATE gateway_support runtime_core)

add_backend_executable(login_server
    apps/login/main.cpp
    apps/login/login_server_app.cpp
)
target_link_libraries(login_server PRIVATE login_application login_infrastructure runtime_core)

add_backend_executable(player_server
    apps/player/main.cpp
    apps/player/player_server_app.cpp
)
target_link_libraries(player_server PRIVATE player_application player_infrastructure runtime_core)

add_backend_executable(dungeon_server
    apps/dungeon/main.cpp
    apps/dungeon/dungeon_server_app.cpp
)
target_link_libraries(dungeon_server PRIVATE dungeon_application dungeon_infrastructure player_infrastructure runtime_core)

add_executable(service_check
    tools/service_check/main.cpp
)
set_backend_target_defaults(service_check)
target_link_libraries(service_check PRIVATE runtime_core)

add_executable(password_tool
    tools/password_tool/main.cpp
)
set_backend_target_defaults(password_tool)
target_link_libraries(password_tool PRIVATE runtime_core)

add_executable(demo_client
    tools/demo_client/main.cpp
)
set_backend_target_defaults(demo_client)
target_link_libraries(demo_client PRIVATE login_infrastructure runtime_core)

add_executable(demo_flow
    tools/demo_flow/main.cpp
)
set_backend_target_defaults(demo_flow)
target_link_libraries(demo_flow
    PRIVATE
        login_application
        login_infrastructure
        player_application
        player_infrastructure
        dungeon_application
        dungeon_infrastructure
        runtime_core
)

add_executable(login_service_test
    tests/login_service_test.cpp
)
set_backend_target_defaults(login_service_test)
target_link_libraries(login_service_test PRIVATE login_application login_infrastructure runtime_core)

add_executable(player_service_test
    tests/player_service_test.cpp
)
set_backend_target_defaults(player_service_test)
target_link_libraries(player_service_test PRIVATE player_application player_infrastructure runtime_core)

add_executable(dungeon_service_test
    tests/dungeon_service_test.cpp
)
set_backend_target_defaults(dungeon_service_test)
target_link_libraries(dungeon_service_test PRIVATE dungeon_application dungeon_infrastructure runtime_core)

add_executable(session_binding_service_test
    tests/session_binding_store_test.cpp
)
set_backend_target_defaults(session_binding_service_test)
target_link_libraries(session_binding_service_test PRIVATE gateway_support runtime_core)

add_executable(packet_codec_test
    tests/packet_codec_test.cpp
)
set_backend_target_defaults(packet_codec_test)
target_link_libraries(packet_codec_test PRIVATE runtime_core)

add_executable(request_dispatcher_test
    tests/request_dispatcher_test.cpp
)
set_backend_target_defaults(request_dispatcher_test)
target_link_libraries(request_dispatcher_test PRIVATE runtime_core)

add_executable(sharded_request_executor_test
    tests/sharded_request_executor_test.cpp
)
set_backend_target_defaults(sharded_request_executor_test)
target_link_libraries(sharded_request_executor_test PRIVATE runtime_core)

add_executable(trusted_gateway_signature_test
    tests/trusted_gateway_signature_test.cpp
)
set_backend_target_defaults(trusted_gateway_signature_test)
target_link_libraries(trusted_gateway_signature_test PRIVATE runtime_core)

add_executable(transport_tls_test
    tests/transport_tls_test.cpp
)
set_backend_target_defaults(transport_tls_test)
target_link_libraries(transport_tls_test PRIVATE runtime_core)

add_executable(service_check_tls_config_test
    tests/service_check_tls_config_test.cpp
)
set_backend_target_defaults(service_check_tls_config_test)
target_link_libraries(service_check_tls_config_test PRIVATE runtime_core)
target_compile_definitions(service_check_tls_config_test PRIVATE PROJECT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(service_check_modes_test
    tests/service_check_modes_test.cpp
)
set_backend_target_defaults(service_check_modes_test)
target_link_libraries(service_check_modes_test PRIVATE runtime_core)

add_executable(upstream_response_validator_test
    tests/upstream_response_validator_test.cpp
)
set_backend_target_defaults(upstream_response_validator_test)
target_link_libraries(upstream_response_validator_test PRIVATE gateway_support runtime_core)

add_executable(observability_log_test
    tests/observability_log_test.cpp
)
set_backend_target_defaults(observability_log_test)
target_link_libraries(observability_log_test PRIVATE runtime_core)

add_executable(logger_test
    tests/logger_test.cpp
)
set_backend_target_defaults(logger_test)
target_link_libraries(logger_test PRIVATE runtime_core)

if(BUILD_TESTING)
    add_test(
        NAME architecture_guard
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_architecture.py ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
add_test(NAME login_service_test COMMAND login_service_test)
add_test(NAME player_service_test COMMAND player_service_test)
add_test(NAME dungeon_service_test COMMAND dungeon_service_test)
add_test(NAME session_binding_service_test COMMAND session_binding_service_test)
add_test(NAME packet_codec_test COMMAND packet_codec_test)
add_test(NAME request_dispatcher_test COMMAND request_dispatcher_test)
add_test(NAME sharded_request_executor_test COMMAND sharded_request_executor_test)
add_test(NAME trusted_gateway_signature_test COMMAND trusted_gateway_signature_test)
add_test(NAME transport_tls_test COMMAND transport_tls_test)
add_test(NAME service_check_tls_config_test COMMAND service_check_tls_config_test)
add_test(NAME service_check_modes_test COMMAND service_check_modes_test)
add_test(NAME upstream_response_validator_test COMMAND upstream_response_validator_test)
add_test(NAME observability_log_test COMMAND observability_log_test)
add_test(NAME logger_test COMMAND logger_test)
