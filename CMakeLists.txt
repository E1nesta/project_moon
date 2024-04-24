cmake_minimum_required(VERSION 3.10)  
project(MyProject
        VERSION 0.0.1
        DESCRIPTION "MyProject" 
        HOMEPAGE_URL "https://github.com/E1nesta/project_moon"
        LANGUAGES CXX)                

set(CMAKE_CXX_STANDARD 11)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin )
set(Process_Dir ${CMAKE_SOURCE_DIR}/src/process)
set(Module_Dir ${CMAKE_SOURCE_DIR}/src/module)

add_subdirectory(src)