# ArchitectGen

ArchitectGen 是一个基于 Qt Widgets 和 CMake 的 C++ 架构代码生成工具。

它提供图形界面来维护类、函数、成员变量等元数据，并根据业务域模板生成对应的 C++ 头文件、源文件以及可复用的库工程脚手架。

## 主要功能

- 通过界面维护类结构、函数参数、成员变量和访问控制
- 基于 templates 目录中的领域模板生成代码
- 实时预览头文件和源文件内容
- 将生成结果输出到 templates 下的各业务域目录
- 自动生成 templates 下的 CMake 工程，使生成结果可作为子工程或安装后的库复用

## 项目结构

- src/GenCore: 代码生成核心逻辑
- src/ArchBuilder: Qt 图形界面程序
- templates: 模板目录，同时也是生成代码和生成库工程的输出目录
- resources: 图标和 Qt 资源文件

## 构建要求

- CMake 3.16 或更高版本
- Qt 5 或 Qt 6，至少包含 Core 和 Widgets 组件
- 支持 C++17 的编译器

## 构建方式

```powershell
cmake -S . -B build
cmake --build build
```

构建完成后会生成主程序 ArchitectGen。

## 使用说明

1. 启动 ArchitectGen
2. 选择或确认输出目录
3. 在左侧业务域树中创建类
4. 编辑函数、参数和成员变量
5. 生成当前类或全部类

生成后的代码会落到 templates/<业务域>/ 目录下。

## 关于 templates 目录

根目录下的本项目是生成器本身。

templates 目录有两层职责：

- 保存各业务域的代码模板
- 保存生成后的类文件和对应的库工程 CMake 文件

如果你想把生成结果当作库复用，可以查看 templates 目录下的说明文件。

## 相关说明

- 根工程 README 说明的是生成器应用本身
- templates/README 说明的是生成出来的库工程如何被其他项目使用
