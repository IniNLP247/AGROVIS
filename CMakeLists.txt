cmake_minimum_required(VERSION 3.22)

project(AGROVIS
    VERSION 0.1.0
    DESCRIPTION "AGROVIS Multispectral Agronomy Workbench"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AGROVIS_BUILD_APP "Build Qt 6 desktop application" ON)
option(AGROVIS_BUILD_TESTS "Build core unit tests" ON)

add_subdirectory(core)

if(AGROVIS_BUILD_APP)
    add_subdirectory(app)
endif()

if(AGROVIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
