project(process)
add_library(${PROJECT_NAME}
SHARED
*.cpp)

target_include_directories(${PROJECT_NAME}
PUBLIC
${Module_Dir})

target_link_libraries(${PROJECT_NAME}
PUBLIC
module)