add_library(GenCore STATIC
    MetaTypes.h
    GeneratorEngine.h GeneratorEngine.cpp
    # 如果有 TemplateManager 等额外类可以在这里添加
)

target_link_libraries(GenCore PUBLIC Qt${QT_VERSION_MAJOR}::Core)
target_include_directories(GenCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})