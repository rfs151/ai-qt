find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(tst_topology tst_topology.cpp)
target_link_libraries(tst_topology PRIVATE app_core Qt6::Test)
target_compile_definitions(tst_topology PRIVATE
    FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../resources/fonts/NotoSansSC-subset.otf"
    SCREENSHOT_DIR="${CMAKE_BINARY_DIR}")

add_test(NAME tst_topology COMMAND tst_topology)
set_tests_properties(tst_topology PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_comm tst_comm.cpp)
target_link_libraries(tst_comm PRIVATE comm_core Qt6::Test)
target_compile_definitions(tst_comm PRIVATE DEVSIM_PATH="$<TARGET_FILE:devsim>")
add_dependencies(tst_comm devsim)
add_test(NAME tst_comm COMMAND tst_comm)

qt_add_executable(tst_history tst_history.cpp)
target_link_libraries(tst_history PRIVATE history_core Qt6::Test)
add_test(NAME tst_history COMMAND tst_history)

qt_add_executable(tst_modbus tst_modbus.cpp)
target_link_libraries(tst_modbus PRIVATE comm_core Qt6::Test)
target_compile_definitions(tst_modbus PRIVATE DEVSIM_PATH="$<TARGET_FILE:devsim>")
add_dependencies(tst_modbus devsim)
# 用 QtSerialBus 的官方 Modbus 实现做互操作测试（只在测试里用，应用本身不依赖它）
find_package(Qt6 QUIET COMPONENTS SerialBus)
if(TARGET Qt6::SerialBus)
    target_link_libraries(tst_modbus PRIVATE Qt6::SerialBus)
    target_compile_definitions(tst_modbus PRIVATE HAS_SERIALBUS)
endif()
add_test(NAME tst_modbus COMMAND tst_modbus)
