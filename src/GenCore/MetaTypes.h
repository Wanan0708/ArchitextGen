#ifndef METATYPES_H
#define METATYPES_H

#include <QString>
#include <QList>
#include <QMetaType>

// 参数元数据
struct ParamMeta {
    QString type;       // 例如: int, QString, MyClass*
    QString name;       // 例如: count, config
    QString defaultValue; // 例如: 0, "", nullptr
    bool isConstRef;    // 是否 const & 传递
};

// 函数元数据
struct FunctionMeta {
    QString name;
    QString returnType; // 例如: void, bool, int
    QList<ParamMeta> parameters;
    QString access = "public";
    bool isConst;       // 成员函数是否 const
    bool isStatic;      // 是否是静态函数
    bool isVirtual;     // 是否是虚函数
};

// 成员变量元数据
struct MemberMeta {
    QString type;       // 例如: int, QString, MyClass*
    QString name;       // 例如: m_count, m_name
    QString defaultValue; // 例如: 0, "", nullptr
    QString access = "private";
};

// 类元数据
struct ClassMeta {
    QString className;
    QString domainKey;  // 对应文件夹名，如 "Core_Business"
    QString baseClass;  // 继承自谁，如 "QObject", "QWidget"
    QList<FunctionMeta> functions;
    QList<MemberMeta> members;  // 成员变量列表
    
    // 辅助函数：获取生成的头文件名
    QString headerFileName() const { return className + ".h"; }
    QString sourceFileName() const { return className + ".cpp"; }
};

// 注册类型以便在 Qt 信号槽或 QVariant 中使用（如果需要）
Q_DECLARE_METATYPE(ClassMeta)
Q_DECLARE_METATYPE(FunctionMeta)
Q_DECLARE_METATYPE(ParamMeta)
Q_DECLARE_METATYPE(MemberMeta)

#endif // METATYPES_H