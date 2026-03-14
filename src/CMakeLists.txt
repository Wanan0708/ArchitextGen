# 先添加底层引擎模块 (ArchBuilder 依赖它)
add_subdirectory(GenCore)

# 再添加主程序模块
add_subdirectory(ArchBuilder)