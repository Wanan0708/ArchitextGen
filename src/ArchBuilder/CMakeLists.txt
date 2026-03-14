add_executable(ArchitectGen
    main.cpp
    MainWindow.h MainWindow.cpp
    MainWindow.ui
    # 如果有 ParamEditor 等控件继续添加
)

target_link_libraries(ArchitectGen PRIVATE 
    Qt5::Widgets
    GenCore # 关键：链接 GenCore 库
)

target_include_directories(ArchitectGen PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/GenCore # 确保能 include MetaTypes.h
)