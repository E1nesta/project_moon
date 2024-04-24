cmake_minimum_required(VERSION 3.10)  
project(MyProject)              
set(PROJECT_N MyProject)        
project(${PROJECT_N} VERSION 1.0)   

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

aux_source_directory(${PROJECT_SOURCE_DIR/src} SRC_LIST)

# 包含项目的头文件目录
include_directories(${PROJECT_SOURCE_DIR}/include)

#动态库和可执行文件输出目录（Windows）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin )

#动态库和可执行文件输出目录（Linux）
#set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/lib )
#静态库输出目录
#set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/lib )
add_library(
    common
    SHARED
    ${SRC_LIST}
 )

 target_include_directories(
    common 
    PUBLIC 
    ${PROJECT_SOURCE_DIR}/include)

#可执行文件
add_executable(${PROJECT_N}  main.cpp)
target_include_directories(
    ${PROJECT_N} 
    PUBLIC 
    ${PROJECT_SOURCE_DIR}/include)
#链接库
target_link_libraries(
    ${PROJECT_N}
    PUBLIC
    common )