add_executable(agrovis_core_tests
    core_tests.cpp
)

target_link_libraries(agrovis_core_tests PRIVATE agrovis::core)

add_test(NAME agrovis_core_tests COMMAND agrovis_core_tests)
