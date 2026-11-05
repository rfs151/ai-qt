cmake_minimum_required(VERSION 3.21)
project(TopologyEditor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

# 业务代码编成静态库，应用和单元测试共用
qt_add_library(topology_core STATIC
    src/MainWindow.h            src/MainWindow.cpp
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
)
target_include_directories(topology_core PUBLIC src)
target_link_libraries(topology_core PUBLIC Qt6::Widgets)

qt_add_executable(topology WIN32 MACOSX_BUNDLE src/main.cpp)
target_link_libraries(topology PRIVATE topology_core)

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
    enable_testing()
    add_subdirectory(tests)
endif()
