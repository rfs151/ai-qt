cmake_minimum_required(VERSION 3.21)
project(TopologyEditor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.6 REQUIRED COMPONENTS Widgets Sql Quick)
if(NOT EMSCRIPTEN)
    # 浏览器里没有串口和原生 TCP，WASM 版只用虚拟设备
    find_package(Qt6 6.5 OPTIONAL_COMPONENTS Network SerialPort)
endif()
qt_standard_project_setup()

# 通信层：只依赖 QtCore（可选 Network/SerialPort），界面、单元测试和 devsim 模拟器共用
qt_add_library(comm_core STATIC
    src/comm/Protocol.h         src/comm/Protocol.cpp
    src/comm/CommTypes.h
    src/comm/DeviceSim.h        src/comm/DeviceSim.cpp
    src/comm/CommWorker.h       src/comm/CommWorker.cpp
    src/comm/CommManager.h      src/comm/CommManager.cpp
    src/comm/TelemetryStore.h   src/comm/TelemetryStore.cpp
    src/comm/Modbus.h           src/comm/Modbus.cpp
)
target_include_directories(comm_core PUBLIC src)
target_link_libraries(comm_core PUBLIC Qt6::Core)
if(TARGET Qt6::Network)
    target_link_libraries(comm_core PUBLIC Qt6::Network)
    target_compile_definitions(comm_core PUBLIC HAS_NETWORK)
endif()
if(TARGET Qt6::SerialPort)
    target_link_libraries(comm_core PUBLIC Qt6::SerialPort)
    target_compile_definitions(comm_core PUBLIC HAS_SERIALPORT)
endif()

# 历史存储：SQLite，独立写库线程
qt_add_library(history_core STATIC
    src/history/HistoryDb.h     src/history/HistoryDb.cpp
)
target_link_libraries(history_core PUBLIC comm_core Qt6::Sql)

# 业务代码编成静态库，应用和单元测试共用
qt_add_library(topology_core STATIC
    src/TopologyTypes.h         src/TopologyTypes.cpp
    src/scene/NodeItem.h        src/scene/NodeItem.cpp
    src/scene/EdgeItem.h        src/scene/EdgeItem.cpp
    src/scene/TopologyScene.h   src/scene/TopologyScene.cpp
    src/scene/TopologyView.h    src/scene/TopologyView.cpp
    src/scene/MiniMapView.h     src/scene/MiniMapView.cpp
    src/commands/Commands.h     src/commands/Commands.cpp
    src/model/PropertyModel.h   src/model/PropertyModel.cpp
    src/model/PropertyDelegate.h src/model/PropertyDelegate.cpp
    src/model/AlarmLogModel.h   src/model/AlarmLogModel.cpp
    src/sim/AlarmSimulator.h    src/sim/AlarmSimulator.cpp
    src/io/TopologyIO.h         src/io/TopologyIO.cpp
    src/monitor/DeviceMonitor.h src/monitor/DeviceMonitor.cpp
    src/widgets/RealtimeChart.h src/widgets/RealtimeChart.cpp
    src/widgets/DevicePanel.h   src/widgets/DevicePanel.cpp
    src/widgets/GaugeWidget.h   src/widgets/GaugeWidget.cpp
)
target_include_directories(topology_core PUBLIC src)
target_link_libraries(topology_core PUBLIC Qt6::Widgets comm_core history_core)

# 监控大屏（Qt Quick）
add_subdirectory(src/dashboard)

# 主窗口：装配 Widgets 编辑器和 QML 大屏
qt_add_library(app_core STATIC
    src/MainWindow.h            src/MainWindow.cpp
)
target_link_libraries(app_core PUBLIC topology_core dashboard dashboardplugin)

qt_add_executable(topology WIN32 MACOSX_BUNDLE src/main.cpp)
target_link_libraries(topology PRIVATE app_core)

# 中文字体子集（scripts/make-font.sh 生成）。WASM 没有系统字体，必须打包进 qrc。
set(EMBED_FONT ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/NotoSansSC-subset.otf)
if(EXISTS ${EMBED_FONT})
    qt_add_resources(topology "fonts"
        PREFIX "/"
        BASE resources
        FILES resources/fonts/NotoSansSC-subset.otf)
    target_compile_definitions(topology PRIVATE HAS_EMBEDDED_FONT)
elseif(EMSCRIPTEN)
    message(WARNING "未找到中文字体子集，WASM 版中文会显示为方块。先运行 scripts/make-font.sh")
endif()

if(EMSCRIPTEN)
    # 初始内存给足，减少 1 万节点压测时的内存增长次数
    set_target_properties(topology PROPERTIES QT_WASM_INITIAL_MEMORY "128MB")
endif()

if(NOT EMSCRIPTEN)
    # 独立的设备模拟器：TCP 服务端或串口，桌面版可以真实连接它
    qt_add_executable(devsim tools/devsim/main.cpp)
    target_link_libraries(devsim PRIVATE comm_core)

    enable_testing()
    add_subdirectory(tests)
endif()
