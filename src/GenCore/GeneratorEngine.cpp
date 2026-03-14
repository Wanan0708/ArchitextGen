#include "GeneratorEngine.h"
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QFileInfo>
#include <QDebug>
#include <QRegularExpression>
#include <QCoreApplication>

namespace {
QString buildIncludeGuard(const QString &className)
{
    QString guard = className.toUpper();
    guard.replace(QRegularExpression("[^A-Z0-9]+"), "_");
    if (!guard.endsWith("_H")) {
        guard += "_H";
    }
    return guard;
}

QString buildBaseClassInclude(const QString &baseClass)
{
    const QString trimmed = baseClass.trimmed();
    if (trimmed.isEmpty() || trimmed == "QObject") {
        return QString();
    }

    if (trimmed.startsWith('Q') && !trimmed.contains("::") && !trimmed.contains('<')) {
        return QString("#include <%1>\n").arg(trimmed);
    }

    if (trimmed.contains("::") || trimmed.contains('<')) {
        return QString();
    }

    return QString("#include \"%1.h\"\n").arg(trimmed);
}

QString normalizeAccessSpecifier(const QString &value, const QString &fallback)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == "public" || normalized == "protected" || normalized == "private") {
        return normalized;
    }
    return fallback;
}

QString buildParentType(const QString &baseClass)
{
    const QString trimmed = baseClass.trimmed();
    if (trimmed.contains("Widget") || trimmed.contains("Window") || trimmed.contains("Dialog") || trimmed.contains("Frame")) {
        return "QWidget *";
    }
    return "QObject *";
}

QString buildParameterString(const ParamMeta &param, bool includeDefaultValue)
{
    QString type = param.type.trimmed();
    if (param.isConstRef) {
        if (!type.startsWith("const ")) {
            type = "const " + type;
        }
        if (!type.endsWith('&')) {
            type += " &";
        }
    }

    QString text = type + " " + param.name;
    if (includeDefaultValue && !param.defaultValue.isEmpty()) {
        text += " = " + param.defaultValue;
    }
    return text;
}

QStringList buildParameterList(const QList<ParamMeta> &parameters, bool includeDefaultValue)
{
    QStringList items;
    for (const auto &param : parameters) {
        items << buildParameterString(param, includeDefaultValue);
    }
    return items;
}

QString buildFunctionDeclaration(const FunctionMeta &func, bool includeDefaultValue)
{
    QString declaration;
    if (func.isStatic) {
        declaration += "static ";
    }
    if (func.isVirtual) {
        declaration += "virtual ";
    }

    declaration += func.returnType + " " + func.name + "(" + buildParameterList(func.parameters, includeDefaultValue).join(", ") + ")";
    if (func.isConst) {
        declaration += " const";
    }
    return declaration;
}

QString buildMemberDeclaration(const MemberMeta &member)
{
    QString declaration = member.type + " " + member.name;
    if (!member.defaultValue.isEmpty()) {
        declaration += " = " + member.defaultValue;
    }
    declaration += ";";
    return declaration;
}

QString buildSection(const QString &access, const QStringList &lines)
{
    if (lines.isEmpty()) {
        return QString();
    }

    QStringList section;
    section << access + ":";
    for (const auto &line : lines) {
        section << "    " + line;
    }
    return section.join("\n") + "\n";
}

QString buildClassBody(const ClassMeta &meta)
{
    QStringList publicLines;
    publicLines << QString("explicit %1(%2parent = nullptr);").arg(meta.className, buildParentType(meta.baseClass));
    publicLines << QString("virtual ~%1();").arg(meta.className);

    QStringList protectedLines;
    QStringList privateLines;
    QStringList publicMemberLines;
    QStringList protectedMemberLines;
    QStringList privateMemberLines;

    for (const auto &func : meta.functions) {
        const QString access = normalizeAccessSpecifier(func.access, "public");
        const QString declaration = buildFunctionDeclaration(func, true) + ";";
        if (access == "protected") {
            protectedLines << declaration;
        } else if (access == "private") {
            privateLines << declaration;
        } else {
            publicLines << declaration;
        }
    }

    for (const auto &member : meta.members) {
        const QString access = normalizeAccessSpecifier(member.access, "private");
        const QString declaration = buildMemberDeclaration(member);
        if (access == "public") {
            publicMemberLines << declaration;
        } else if (access == "protected") {
            protectedMemberLines << declaration;
        } else {
            privateMemberLines << declaration;
        }
    }

    if (!publicMemberLines.isEmpty()) {
        publicLines << publicMemberLines;
    }
    if (!protectedMemberLines.isEmpty()) {
        protectedLines << protectedMemberLines;
    }
    if (!privateMemberLines.isEmpty()) {
        privateLines << privateMemberLines;
    }

    QString body;
    body += buildSection("public", publicLines);
    if (!protectedLines.isEmpty()) {
        body += "\n" + buildSection("protected", protectedLines);
    }
    if (!privateLines.isEmpty()) {
        body += "\n" + buildSection("private", privateLines);
    }

    return body.trimmed() + "\n";
}

QString buildFunctionDefinition(const ClassMeta &meta, const FunctionMeta &func)
{
    QString definition = func.returnType + " " + meta.className + "::" + func.name + "(" + buildParameterList(func.parameters, false).join(", ") + ")";
    if (func.isConst) {
        definition += " const";
    }

    definition += "\n{\n    // TODO: Implement logic\n";
    if (func.returnType.trimmed() != "void") {
        definition += "    return {};\n";
    }
    definition += "}\n";
    return definition;
}

QString functionDefinitionSignature(const ClassMeta &meta, const FunctionMeta &func)
{
    QString signature = func.returnType + " " + meta.className + "::" + func.name + "(" + buildParameterList(func.parameters, false).join(", ") + ")";
    if (func.isConst) {
        signature += " const";
    }
    return signature;
}
}

GeneratorEngine::GeneratorEngine(QObject *parent) : QObject(parent) {}

QString GeneratorEngine::previewHeader(const ClassMeta &meta)
{
    return renderHeader(meta);
}

QString GeneratorEngine::previewSource(const ClassMeta &meta)
{
    return renderSource(meta, QString());
}

bool GeneratorEngine::generateClass(const ClassMeta &meta, const QString &projectRootPath)
{
    if (meta.className.isEmpty()) {
        emit logMessage("Error: Class name is empty.");
        return false;
    }

    // 1. 构建目标路径: {Root}/src/{Domain}/{ClassName}.h/cpp
    QString domainDir = meta.domainKey;
    QString targetDirPath = QDir(projectRootPath).filePath(QDir::cleanPath(domainDir));
    
    // 确保目录存在
    QDir dir(targetDirPath);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            emit logMessage(QString("Error: Cannot create directory %1").arg(targetDirPath));
            return false;
        }
    }

    QString hFilePath = dir.filePath(meta.className + ".h");
    QString cppFilePath = dir.filePath(meta.className + ".cpp");

    emit logMessage(QString("Generating: %1").arg(meta.className));

    // 2. 生成头文件 (.h)
    // 策略：头文件通常由工具完全管理，直接覆盖（或者也可以做简单的标记保护，这里简化为覆盖）
    QString hContent = renderHeader(meta);
    QFile hFile(hFilePath);
    if (!hFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit logMessage(QString("Error writing header: %1").arg(hFile.errorString()));
        return false;
    }
    QTextStream outH(&hFile);
    outH << hContent;
    hFile.close();

    // 3. 生成源文件 (.cpp)
    // 策略：增量生成。如果文件存在，读取旧内容，只追加新函数，保留用户写的逻辑
    QString existingCpp = "";
    if (QFile::exists(cppFilePath)) {
        QFile oldFile(cppFilePath);
        if (oldFile.open(QIODevice::ReadOnly)) {
            existingCpp = oldFile.readAll();
            oldFile.close();
        }
    }
    
    QString cppContent = renderSource(meta, existingCpp);
    QFile cppFile(cppFilePath);
    if (!cppFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit logMessage(QString("Error writing source: %1").arg(cppFile.errorString()));
        return false;
    }
    QTextStream outCpp(&cppFile);
    outCpp << cppContent;
    cppFile.close();

    emit logMessage(QString("Success: Generated %1").arg(meta.className));
    return true;
}

QString GeneratorEngine::loadTemplate(const QString &domainKey, const QString &fileName)
{
    // 尝试从项目根目录下的templates文件夹加载模板
    // 可执行文件在 build/src/ArchBuilder/，项目根目录是 e:/Project/VSCodeProject/ArchitextGen
    // 模板路径: {projectRoot}/templates/{domainKey}/{fileName}
    
    QString appDir = QCoreApplication::applicationDirPath();
    
    // 尝试多个可能的路径
    QStringList possiblePaths;
    possiblePaths << appDir + "/../../../templates/" + domainKey + "/" + fileName;
    possiblePaths << appDir + "/../../templates/" + domainKey + "/" + fileName;
    possiblePaths << appDir + "/../templates/" + domainKey + "/" + fileName;
    possiblePaths << appDir + "/templates/" + domainKey + "/" + fileName;
    
    for (const QString &templatePath : possiblePaths) {
        QFile f(templatePath);
        if (f.open(QIODevice::ReadOnly)) {
            qDebug() << "Template loaded from:" << templatePath;
            return f.readAll();
        }
    }
    
    // 如果找不到特定域的模板，使用默认模板
    qWarning() << "Template not found for domain:" << domainKey << "file:" << fileName;
    return QString();
}

QString GeneratorEngine::renderHeader(const ClassMeta &meta)
{
    QString tpl = loadTemplate(meta.domainKey, "class.h.tpl");
    
    // 如果模板不存在，生成默认的类文件
    if (tpl.isEmpty()) {
        const QString guardName = buildIncludeGuard(meta.className);
        QString baseClassInclude = buildBaseClassInclude(meta.baseClass);
        if (baseClassInclude.isEmpty()) {
            baseClassInclude = "#include <QObject>\n";
        }
        
        tpl = "#ifndef " + guardName + "\n"
              "#define " + guardName + "\n\n"
              + baseClassInclude + "\n"
              "class " + meta.className + " : public " + meta.baseClass + "\n"
              "{\n"
              + buildClassBody(meta) + "\n";

        tpl += "};\n\n"
              "#endif // " + guardName + "\n";
    } else {
        tpl = replacePlaceholders(tpl, meta);
    }
    
    return tpl;
}

QString GeneratorEngine::renderSource(const ClassMeta &meta, const QString &existingContent)
{
    // 尝试加载模板
    QString tpl = loadTemplate(meta.domainKey, "class.cpp.tpl");
    
    // 新建文件时优先使用模板；已有文件时只追加缺失实现，避免覆盖用户代码。
    if (!tpl.isEmpty() && existingContent.isEmpty()) {
        return replacePlaceholders(tpl, meta);
    }

    if (!existingContent.isEmpty()) {
        QString mergedContent = existingContent;
        QStringList missingDefinitions;
        for (const auto &func : meta.functions) {
            const QString signature = functionDefinitionSignature(meta, func);
            if (!mergedContent.contains(signature)) {
                missingDefinitions << buildFunctionDefinition(meta, func);
            }
        }

        if (missingDefinitions.isEmpty()) {
            return mergedContent;
        }

        if (!mergedContent.endsWith('\n')) {
            mergedContent += "\n";
        }
        mergedContent += "\n// --- Generated Stubs ---\n";
        mergedContent += missingDefinitions.join("\n");
        return mergedContent;
    }
    
    // 模板中没有占位符或模板不存在，生成默认的cpp文件内容
    QString cppContent = "#include \"" + meta.className + ".h\"\n\n";
    
    // 构造函数
    cppContent += meta.className + "::" + meta.className + "(" + buildParentType(meta.baseClass) + "parent)\n";
    cppContent += "    : " + meta.baseClass + "(parent)\n";
    cppContent += "{\n}\n\n";
    
    // 析构函数
    cppContent += meta.className + "::~" + meta.className + "()\n";
    cppContent += "{\n}\n\n";
    
    // 添加函数实现
    for (const auto &func : meta.functions) {
        cppContent += buildFunctionDefinition(meta, func) + "\n";
    }
    
    return cppContent;
}

QString GeneratorEngine::replacePlaceholders(QString content, const ClassMeta &meta)
{
    content.replace("{{CLASS_NAME}}", meta.className);
    content.replace("{{BASE_CLASS}}", meta.baseClass);
    content.replace("{{DOMAIN_KEY}}", meta.domainKey);
    content.replace("{{BASE_CLASS_INCLUDE}}", buildBaseClassInclude(meta.baseClass));
    content.replace("{{PARENT_TYPE}}", buildParentType(meta.baseClass));
    
    // 生成 Include Guard
    QString guardName = buildIncludeGuard(meta.className);
    content.replace("{{INCLUDE_GUARD}}", guardName);
    content.replace("{{CLASS_BODY}}", buildClassBody(meta));

    // 生成函数声明列表 (用于头文件)
    QString funcDeclarations;
    for (const auto &func : meta.functions) {
        QString line = QString("    ") + buildFunctionDeclaration(func, true) + ";\n";
        funcDeclarations += line;
    }
    content.replace("{{FUNCTION_DECLARATIONS}}", funcDeclarations);
    
    // 生成函数实现列表 (用于源文件)
    QString funcImplementations;
    for (const auto &func : meta.functions) {
        funcImplementations += buildFunctionDefinition(meta, func) + "\n";
    }
    content.replace("{{FUNCTION_IMPLEMENTATIONS}}", funcImplementations);
    
    // 生成成员变量声明列表
    QString memberDeclarations;
    for (const auto &member : meta.members) {
        QString line = QString("    ") + buildMemberDeclaration(member) + "\n";
        memberDeclarations += line;
    }
    content.replace("{{MEMBER_DECLARATIONS}}", memberDeclarations);

    return content;
}