set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Quick QuickControls2)

qt_add_executable(agrovis_app
    src/main.cpp
    src/AppController.h
    src/AppController.cpp
    src/CaptureListModel.h
    src/CaptureListModel.cpp
    src/ViewportImageProvider.h
    src/ViewportImageProvider.cpp
)

qt_add_qml_module(agrovis_app
    URI Agrovis
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    QML_FILES
        qml/Main.qml
        qml/CaptureSidebar.qml
        qml/SpectralViewport.qml
        qml/AnalysisInspector.qml
)

target_link_libraries(agrovis_app
    PRIVATE
        agrovis::core
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::QuickControls2
)

target_compile_features(agrovis_app PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(agrovis_app PRIVATE /W4)
else()
    target_compile_options(agrovis_app PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(agrovis_app PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)
