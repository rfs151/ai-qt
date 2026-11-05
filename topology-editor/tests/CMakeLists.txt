find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(tst_topology tst_topology.cpp)
target_link_libraries(tst_topology PRIVATE topology_core Qt6::Test)
target_compile_definitions(tst_topology PRIVATE
    FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../resources/fonts/NotoSansSC-subset.otf"
    SCREENSHOT_DIR="${CMAKE_BINARY_DIR}")

add_test(NAME tst_topology COMMAND tst_topology)
set_tests_properties(tst_topology PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
