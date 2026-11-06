# 监控大屏：QML 模块 Topology.Dashboard（C++ 类型 + QML 文件一起编译）。
# QML 文件经 qmlcachegen 预编译成 C++，运行时不用再解析源码。
qt_add_library(dashboard STATIC)

set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(dashboard
    URI Topology.Dashboard
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    DEPENDENCIES QtQuick
    # 输出目录和 URI 对应（Topology/Dashboard），qmllint 和工具链才能按导入路径找到模块
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Topology/Dashboard
    SOURCES
        DashboardBackend.h  DashboardBackend.cpp
        DeviceListModel.h   DeviceListModel.cpp
        TrendItem.h         TrendItem.cpp
    QML_FILES
        Dashboard.qml
        Theme.qml
        Panel.qml
        DashButton.qml
        KpiCard.qml
        ArcSegment.qml
        RingGauge.qml
        StatusDonut.qml
        TrendChart.qml
        AlarmFeed.qml
        DeviceCard.qml
)
target_include_directories(dashboard PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(dashboard PUBLIC topology_core Qt6::Quick)
