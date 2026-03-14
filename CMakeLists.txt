cmake_minimum_required(VERSION 3.16)
project(ArchitectGen VERSION 1.0 LANGUAGES CXX)

# 设置 C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 启用 Qt 的自动处理 (MOC, UIC, RCC)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 查找 Qt 组件 (需要 Core 和 Widgets)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets)

# 添加源码子目录 (包含 GenCore 和 ArchBuilder)
add_subdirectory(src)
add_subdirectory(templates)

# 【可选】将 templates 文件夹复制到构建目录，方便开发时直接修改模板测试
# 正式发布时通常会将模板打包进 .qrc 资源文件，但开发阶段复制出来更方便调试
file(COPY ${CMAKE_SOURCE_DIR}/templates DESTINATION ${CMAKE_BINARY_DIR})