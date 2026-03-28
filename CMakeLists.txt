cmake_minimum_required(VERSION 3.20)

project(mobile_game_backend VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(common STATIC
    common/src/bootstrap/service_app.cpp
    common/src/config/simple_config.cpp
    common/src/log/logger.cpp
)

target_include_directories(common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/common/include
)

target_compile_features(common PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(common PRIVATE /W4 /permissive-)
else()
    target_compile_options(common PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(gateway
    gateway/main.cpp
)
target_link_libraries(gateway PRIVATE common)
target_include_directories(gateway PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(login_server
    login_server/main.cpp
    login_server/login_service.cpp
    login_server/auth/in_memory_account_repository.cpp
    login_server/session/in_memory_session_repository.cpp
)
target_link_libraries(login_server PRIVATE common)
target_include_directories(login_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(game_server
    game_server/main.cpp
    game_server/player/player_service.cpp
    game_server/player/in_memory_player_repository.cpp
)
target_link_libraries(game_server PRIVATE common)
target_include_directories(game_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(demo_flow
    tools/demo_flow/main.cpp
    login_server/login_service.cpp
    login_server/auth/in_memory_account_repository.cpp
    login_server/session/in_memory_session_repository.cpp
    game_server/player/player_service.cpp
    game_server/player/in_memory_player_repository.cpp
)
target_link_libraries(demo_flow PRIVATE common)
target_include_directories(demo_flow PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    target_compile_options(gateway PRIVATE /W4 /permissive-)
    target_compile_options(login_server PRIVATE /W4 /permissive-)
    target_compile_options(game_server PRIVATE /W4 /permissive-)
    target_compile_options(demo_flow PRIVATE /W4 /permissive-)
else()
    target_compile_options(gateway PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(login_server PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(game_server PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(demo_flow PRIVATE -Wall -Wextra -Wpedantic)
endif()
