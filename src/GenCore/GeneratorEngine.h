#ifndef GENERATORENGINE_H
#define GENERATORENGINE_H

#include <QObject>
#include "MetaTypes.h"

class GeneratorEngine : public QObject
{
    Q_OBJECT
public:
    explicit GeneratorEngine(QObject *parent = nullptr);

    // 核心接口：根据元数据生成文件
    // projectRootPath: 项目根目录 (例如 D:/MyProject)
    // 返回 true 表示成功，false 表示失败
    bool generateClass(const ClassMeta &meta, const QString &projectRootPath);

    // 预览渲染结果，不写入磁盘。
    QString previewHeader(const ClassMeta &meta);
    QString previewSource(const ClassMeta &meta);

signals:
    void logMessage(const QString &msg);

private:
    // 内部辅助：渲染头文件内容
    QString renderHeader(const ClassMeta &meta);
    // 内部辅助：渲染源文件内容 (支持增量合并)
    QString renderSource(const ClassMeta &meta, const QString &existingContent);
    // 内部辅助：读取模板文件
    QString loadTemplate(const QString &domainKey, const QString &fileName);
    // 内部辅助：简单的占位符替换
    QString replacePlaceholders(QString content, const ClassMeta &meta);
};

#endif // GENERATORENGINE_H