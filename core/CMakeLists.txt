find_package(OpenCV QUIET COMPONENTS core imgcodecs imgproc video)

add_library(agrovis_core STATIC
    src/capture_scanner.cpp
    src/engine.cpp
    src/radiometry.cpp
    src/registration.cpp
)

add_library(agrovis::core ALIAS agrovis_core)

target_include_directories(agrovis_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(agrovis_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(agrovis_core PRIVATE /W4)
else()
    target_compile_options(agrovis_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(OpenCV_FOUND)
    target_link_libraries(agrovis_core PUBLIC ${OpenCV_LIBS})
    target_compile_definitions(agrovis_core PUBLIC AGROVIS_HAS_OPENCV=1)
else()
    target_compile_definitions(agrovis_core PUBLIC AGROVIS_HAS_OPENCV=0)
endif()
