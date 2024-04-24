project(main)

add_subdirectory(module)
add_subdirectory(process)

add_executable(${PROJECT_NAME} main.cpp)

target_include_directories(${PROJECT_NAME}
PUBLIC
${Process_Dir})