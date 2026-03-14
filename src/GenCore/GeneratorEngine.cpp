#include "GeneratorEngine.h"
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QFileInfo>
#include <QDebug>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QStandardPaths>

namespace {
const char kGeneratedLibraryProjectName[] = "ArchitectGenLibraries";

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
    QString declaration;
    if (member.isConstexpr) {
        declaration += "static constexpr ";
    } else if (member.isStatic) {
        declaration += "static ";
    }

    declaration += member.type + " " + member.name;
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

QString normalizeGeneratedText(QString content)
{
    content.replace("\r\n", "\n");
    content.replace(QRegularExpression("[ \t]+\n"), "\n");
    content.replace(QRegularExpression("\n{3,}"), "\n\n");
    return content.trimmed() + "\n";
}

QStringList sourceFilePatterns()
{
    return {"*.cpp", "*.cc", "*.cxx"};
}

QStringList headerFilePatterns()
{
    return {"*.h", "*.hpp"};
}

QStringList listMatchingFiles(const QString &directoryPath, const QStringList &patterns)
{
    const QDir directory(directoryPath);
    return directory.entryList(patterns, QDir::Files, QDir::Name);
}

QStringList discoverLibraryDomains(const QString &projectRootPath)
{
    QStringList domains;
    const QDir rootDirectory(projectRootPath);
    const QFileInfoList entries = rootDirectory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : entries) {
        if (!listMatchingFiles(entry.absoluteFilePath(), sourceFilePatterns()).isEmpty()) {
            domains.append(entry.fileName());
        }
    }
    return domains;
}

QString indentList(const QStringList &items, const QString &indent)
{
    QString text;
    for (const QString &item : items) {
        text += indent + item + "\n";
    }
    return text;
}

bool writeTextFileIfChanged(const QString &filePath, const QString &content, QString *errorMessage = nullptr)
{
    QFile existingFile(filePath);
    if (existingFile.exists() && existingFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString existingContent = QString::fromUtf8(existingFile.readAll());
        existingFile.close();
        if (existingContent == content) {
            return true;
        }
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    QTextStream stream(&file);
    stream << content;
    file.close();
    return true;
}

QString buildRootCMakeContent(const QStringList &domains)
{
    QString content;
    content += "cmake_minimum_required(VERSION 3.16)\n";
    content += QString("project(%1 VERSION 1.0 LANGUAGES CXX)\n\n").arg(kGeneratedLibraryProjectName);
    content += "set(CMAKE_CXX_STANDARD 17)\n";
    content += "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
    content += "set(CMAKE_AUTOMOC ON)\n";
    content += "set(CMAKE_AUTORCC OFF)\n";
    content += "set(CMAKE_AUTOUIC OFF)\n\n";
    content += "include(GNUInstallDirs)\n\n";
    content += "find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets)\n";
    content += "find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets)\n\n";
    content += "set(ARCHITECTGEN_LIBRARY_DOMAINS\n";
    content += indentList(domains, "    ");
    content += ")\n\n";
    content += "foreach(domain IN LISTS ARCHITECTGEN_LIBRARY_DOMAINS)\n";
    content += "    add_subdirectory(${domain})\n";
    content += "endforeach()\n\n";
    content += "include(CMakePackageConfigHelpers)\n";
    content += "configure_package_config_file(\n";
    content += "    \"${CMAKE_CURRENT_SOURCE_DIR}/cmake/ArchitectGenLibrariesConfig.cmake.in\"\n";
    content += "    \"${CMAKE_CURRENT_BINARY_DIR}/ArchitectGenLibrariesConfig.cmake\"\n";
    content += "    INSTALL_DESTINATION \"${CMAKE_INSTALL_LIBDIR}/cmake/ArchitectGenLibraries\"\n";
    content += ")\n\n";
    content += "write_basic_package_version_file(\n";
    content += "    \"${CMAKE_CURRENT_BINARY_DIR}/ArchitectGenLibrariesConfigVersion.cmake\"\n";
    content += "    VERSION ${PROJECT_VERSION}\n";
    content += "    COMPATIBILITY SameMajorVersion\n";
    content += ")\n\n";
    content += "install(EXPORT ArchitectGenTargets\n";
    content += "    FILE ArchitectGenLibrariesTargets.cmake\n";
    content += "    NAMESPACE ArchitectGen::\n";
    content += "    DESTINATION \"${CMAKE_INSTALL_LIBDIR}/cmake/ArchitectGenLibraries\"\n";
    content += ")\n\n";
    content += "install(FILES\n";
    content += "    \"${CMAKE_CURRENT_BINARY_DIR}/ArchitectGenLibrariesConfig.cmake\"\n";
    content += "    \"${CMAKE_CURRENT_BINARY_DIR}/ArchitectGenLibrariesConfigVersion.cmake\"\n";
    content += "    DESTINATION \"${CMAKE_INSTALL_LIBDIR}/cmake/ArchitectGenLibraries\"\n";
    content += ")\n";
    return content;
}

QString buildDomainCMakeContent(const QString &domainKey, const QStringList &headerFiles, const QStringList &sourceFiles)
{
    QString content;
    content += "# Generated by ArchitectGen. Manual edits may be overwritten.\n\n";
    content += "set(domain_public_headers\n";
    content += indentList(headerFiles, "    ");
    content += ")\n\n";
    content += "set(domain_sources\n";
    content += indentList(sourceFiles, "    ");
    content += ")\n\n";
    content += QString("add_library(%1\n").arg(domainKey);
    content += "    ${domain_sources}\n";
    content += "    ${domain_public_headers}\n";
    content += ")\n\n";
    content += QString("add_library(ArchitectGen::%1 ALIAS %1)\n\n").arg(domainKey);
    content += QString("target_compile_features(%1 PUBLIC cxx_std_17)\n\n").arg(domainKey);
    content += QString("target_include_directories(%1\n").arg(domainKey);
    content += "    PUBLIC\n";
    content += "        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>\n";
    content += QString("        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/ArchitectGen/%1>\n").arg(domainKey);
    content += ")\n\n";
    content += QString("target_link_libraries(%1\n").arg(domainKey);
    content += "    PUBLIC\n";
    content += "        Qt${QT_VERSION_MAJOR}::Core\n";
    content += "        Qt${QT_VERSION_MAJOR}::Widgets\n";
    content += ")\n\n";
    content += QString("install(TARGETS %1\n").arg(domainKey);
    content += "    EXPORT ArchitectGenTargets\n";
    content += "    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}\n";
    content += "    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}\n";
    content += "    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}\n";
    content += ")\n\n";
    content += "install(FILES\n";
    content += "    ${domain_public_headers}\n";
    content += QString("    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ArchitectGen/%1\n").arg(domainKey);
    content += ")\n";
    return content;
}

QString buildPackageConfigTemplateContent()
{
    QString content;
    content += "@PACKAGE_INIT@\n\n";
    content += "include(CMakeFindDependencyMacro)\n\n";
    content += "if(NOT TARGET Qt6::Core AND NOT TARGET Qt5::Core)\n";
    content += "    find_dependency(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets)\n";
    content += "    find_dependency(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets)\n";
    content += "endif()\n\n";
    content += "include(\"${CMAKE_CURRENT_LIST_DIR}/ArchitectGenLibrariesTargets.cmake\")\n";
    return content;
}

QString buildReadmeContent(const QStringList &domains)
{
    QString content;
    content += "# ArchitectGen Libraries\n\n";
    content += "This directory is generated by ArchitectGen and can be consumed as a reusable CMake library workspace.\n\n";
    content += "## Included Domain Libraries\n\n";
    for (const QString &domain : domains) {
        content += "- ArchitectGen::" + domain + "\n";
    }
    content += "\n## Use In Another Project\n\n";
    content += "### Option 1: add_subdirectory\n\n";
    content += "```cmake\n";
    content += "add_subdirectory(path/to/generated/templates external/architectgen)\n";
    content += "target_link_libraries(MyApp PRIVATE ArchitectGen::Data_Persistence)\n";
    content += "```\n\n";
    content += "### Option 2: install and find_package\n\n";
    content += "```powershell\n";
    content += "cmake -S path/to/generated/templates -B build\n";
    content += "cmake --build build\n";
    content += "cmake --install build --prefix path/to/install\n";
    content += "```\n\n";
    content += "```cmake\n";
    content += "find_package(ArchitectGenLibraries CONFIG REQUIRED PATHS path/to/install)\n";
    content += "target_link_libraries(MyApp PRIVATE ArchitectGen::Data_Persistence)\n";
    content += "```\n";
    return content;
}

QString buildClassBody(const ClassMeta &meta)
{
    QString body;
    
    // 如果有命名空间，生成命名空间声明
    if (!meta.namespaceStr.isEmpty()) {
        QStringList nsParts = meta.namespaceStr.split("::");
        for (const QString &ns : nsParts) {
            body += "namespace " + ns + " {\n";
        }
        body += "\n";
    }
    
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

    body += buildSection("public", publicLines);
    if (!protectedLines.isEmpty()) {
        body += "\n" + buildSection("protected", protectedLines);
    }
    if (!privateLines.isEmpty()) {
        body += "\n" + buildSection("private", privateLines);
    }
    
    // 如果有命名空间，添加闭合大括号
    if (!meta.namespaceStr.isEmpty()) {
        QStringList nsParts = meta.namespaceStr.split("::");
        for (int i = nsParts.size() - 1; i >= 0; --i) {
            body += "\n} // namespace " + nsParts[i];
        }
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

struct FunctionDefinitionMatch
{
    int start = -1;
    int openBrace = -1;
    int end = -1;
    QString functionName;
    QString signature;

    bool isValid() const
    {
        return start >= 0 && openBrace >= 0 && end >= openBrace;
    }
};

QString normalizeFunctionSignature(QString signature)
{
    signature.replace("\r\n", "\n");
    signature.replace(QRegularExpression("\\s+"), " ");
    return signature.trimmed();
}

int findMatchingClosingBrace(const QString &content, int openBraceIndex)
{
    if (openBraceIndex < 0 || openBraceIndex >= content.size() || content.at(openBraceIndex) != '{') {
        return -1;
    }

    int depth = 0;
    bool inSingleLineComment = false;
    bool inMultiLineComment = false;
    bool inString = false;
    bool inChar = false;
    bool escaped = false;

    for (int i = openBraceIndex; i < content.size(); ++i) {
        const QChar ch = content.at(i);
        const QChar next = i + 1 < content.size() ? content.at(i + 1) : QChar();

        if (inSingleLineComment) {
            if (ch == '\n') {
                inSingleLineComment = false;
            }
            continue;
        }

        if (inMultiLineComment) {
            if (ch == '*' && next == '/') {
                inMultiLineComment = false;
                ++i;
            }
            continue;
        }

        if (inString) {
            if (!escaped && ch == '"') {
                inString = false;
            }
            escaped = !escaped && ch == '\\';
            continue;
        }

        if (inChar) {
            if (!escaped && ch == '\'') {
                inChar = false;
            }
            escaped = !escaped && ch == '\\';
            continue;
        }

        escaped = false;

        if (ch == '/' && next == '/') {
            inSingleLineComment = true;
            ++i;
            continue;
        }

        if (ch == '/' && next == '*') {
            inMultiLineComment = true;
            ++i;
            continue;
        }

        if (ch == '"') {
            inString = true;
            continue;
        }

        if (ch == '\'') {
            inChar = true;
            continue;
        }

        if (ch == '{') {
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }

    return -1;
}

QList<FunctionDefinitionMatch> findClassFunctionDefinitions(const QString &content, const QString &className)
{
    QList<FunctionDefinitionMatch> matches;
    const QString pattern = QString("(^|\\n)\\s*[^\\n]*\\b%1::([~A-Za-z_][A-Za-z0-9_]*)\\s*\\([^\\n]*\\)\\s*(?:const\\s*)?\\n?\\s*\\{")
                                .arg(QRegularExpression::escape(className));
    QRegularExpression regex(pattern);
    QRegularExpressionMatchIterator iterator = regex.globalMatch(content);
    while (iterator.hasNext()) {
        const QRegularExpressionMatch match = iterator.next();
        const int fullStart = match.capturedStart(0);
        const int start = content.lastIndexOf('\n', fullStart >= 0 ? fullStart : 0) + 1;
        const int openBrace = content.indexOf('{', match.capturedStart(0));
        const int end = findMatchingClosingBrace(content, openBrace);
        if (openBrace < 0 || end < 0) {
            continue;
        }

        FunctionDefinitionMatch definitionMatch;
        definitionMatch.start = start;
        definitionMatch.openBrace = openBrace;
        definitionMatch.end = end;
        definitionMatch.functionName = match.captured(2);
        definitionMatch.signature = normalizeFunctionSignature(content.mid(start, openBrace - start));
        matches.append(definitionMatch);
    }

    return matches;
}

QList<FunctionDefinitionMatch> findFunctionDefinitionsByName(const QString &content, const QString &className, const QString &functionName)
{
    QList<FunctionDefinitionMatch> matches;
    const QList<FunctionDefinitionMatch> allMatches = findClassFunctionDefinitions(content, className);
    for (const FunctionDefinitionMatch &match : allMatches) {
        if (match.functionName == functionName) {
            matches.append(match);
        }
    }
    return matches;
}

bool replaceFunctionDefinitionByName(QString &content, const ClassMeta &meta, const FunctionMeta &func)
{
    const QList<FunctionDefinitionMatch> matches = findFunctionDefinitionsByName(content, meta.className, func.name);
    if (matches.size() != 1 || !matches.first().isValid()) {
        return false;
    }

    const FunctionDefinitionMatch &match = matches.first();
    QString body = content.mid(match.openBrace, match.end - match.openBrace + 1);
    QString replacement = functionDefinitionSignature(meta, func) + "\n" + body;
    content.replace(match.start, match.end - match.start + 1, replacement);
    return true;
}

void removeObsoleteFunctionDefinitions(QString &content, const ClassMeta &meta)
{
    QSet<QString> expectedSignatures;
    for (const FunctionMeta &func : meta.functions) {
        expectedSignatures.insert(normalizeFunctionSignature(functionDefinitionSignature(meta, func)));
    }

    const QList<FunctionDefinitionMatch> matches = findClassFunctionDefinitions(content, meta.className);
    for (int index = matches.size() - 1; index >= 0; --index) {
        const FunctionDefinitionMatch &match = matches.at(index);
        if (!match.isValid()) {
            continue;
        }

        if (match.functionName == meta.className || match.functionName == ("~" + meta.className)) {
            continue;
        }

        if (expectedSignatures.contains(match.signature)) {
            continue;
        }

        int removeEnd = match.end + 1;
        while (removeEnd < content.size() && content.at(removeEnd) == '\n') {
            ++removeEnd;
        }
        content.remove(match.start, removeEnd - match.start);
    }

    content.replace(QRegularExpression("\\n// --- Generated Stubs ---\\n(?=\\s*$)"), "\n");
}
}

GeneratorEngine::GeneratorEngine(QObject *parent) : QObject(parent) {}

QString GeneratorEngine::previewHeader(const ClassMeta &meta)
{
    return normalizeGeneratedText(renderHeader(meta));
}

QString GeneratorEngine::previewSource(const ClassMeta &meta)
{
    return normalizeGeneratedText(renderSource(meta, QString()));
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
    QString hContent = normalizeGeneratedText(renderHeader(meta));
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
    
    QString cppContent = normalizeGeneratedText(renderSource(meta, existingCpp));
    QFile cppFile(cppFilePath);
    if (!cppFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit logMessage(QString("Error writing source: %1").arg(cppFile.errorString()));
        return false;
    }
    QTextStream outCpp(&cppFile);
    outCpp << cppContent;
    cppFile.close();

    if (!generateLibraryProjectFiles(projectRootPath)) {
        emit logMessage("Error: Failed to update generated library CMake files.");
        return false;
    }

    emit logMessage(QString("Success: Generated %1").arg(meta.className));
    return true;
}

QString GeneratorEngine::loadTemplate(const QString &domainKey, const QString &fileName)
{
    // 优先从项目根目录下的templates文件夹加载模板
    // 使用 QStandardPaths 查找项目根目录
    QStringList searchPaths;
    
    // 1. 尝试从应用程序所在目录的相对路径查找
    QString appDir = QCoreApplication::applicationDirPath();
    searchPaths << appDir + "/../../../templates/";
    searchPaths << appDir + "/../../templates/";
    searchPaths << appDir + "/../templates/";
    searchPaths << appDir + "/templates/";
    
    // 2. 尝试从工作目录查找
    searchPaths << QDir::currentPath() + "/templates/";
    
    // 3. 尝试从用户文档目录查找
    QString userDocPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    searchPaths << userDocPath + "/ArchitextGen/templates/";
    
    // 4. 尝试从应用程序资源中查找
    // 如果编译时包含了qrc资源，这可以作为备选
    
    for (const QString &basePath : searchPaths) {
        QString templatePath = basePath + domainKey + "/" + fileName;
        QFile f(templatePath);
        if (f.open(QIODevice::ReadOnly)) {
            qDebug() << "Template loaded from:" << templatePath;
            return f.readAll();
        }
    }
    
    // 如果找不到特定域的模板，尝试使用通用模板
    QString genericPath = appDir + "/../../../templates/Generic/" + fileName;
    QFile genericFile(genericPath);
    if (genericFile.open(QIODevice::ReadOnly)) {
        return genericFile.readAll();
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
        
        // 使用更可靠的函数检测：查找函数定义的开头
        for (const auto &func : meta.functions) {
            const QString signature = functionDefinitionSignature(meta, func);
            QRegularExpression regex("(" + QRegularExpression::escape(signature) + ")\\s*[/[{]");
            if (regex.match(mergedContent).hasMatch()) {
                continue;
            }

            if (replaceFunctionDefinitionByName(mergedContent, meta, func)) {
                continue;
            }

            {
                missingDefinitions << buildFunctionDefinition(meta, func);
            }
        }

        removeObsoleteFunctionDefinitions(mergedContent, meta);

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
    content.replace("{{NAMESPACE}}", meta.namespaceStr);
    content.replace("{{QUALIFIED_NAME}}", meta.qualifiedName());
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

    return normalizeGeneratedText(content);
}

bool GeneratorEngine::generateLibraryProjectFiles(const QString &projectRootPath)
{
    const QStringList domains = discoverLibraryDomains(projectRootPath);
    if (domains.isEmpty()) {
        emit logMessage("Warning: No generated source files found for library scaffolding.");
        return true;
    }

    const QDir rootDirectory(projectRootPath);
    QString errorMessage;

    const QString cmakeDirectoryPath = rootDirectory.filePath("cmake");
    if (!QDir().mkpath(cmakeDirectoryPath)) {
        emit logMessage(QString("Error: Cannot create CMake helper directory %1").arg(cmakeDirectoryPath));
        return false;
    }

    for (const QString &domain : domains) {
        const QString domainPath = rootDirectory.filePath(domain);
        const QStringList headerFiles = listMatchingFiles(domainPath, headerFilePatterns());
        const QStringList sourceFiles = listMatchingFiles(domainPath, sourceFilePatterns());
        if (sourceFiles.isEmpty()) {
            continue;
        }

        const QString domainCMakePath = QDir(domainPath).filePath("CMakeLists.txt");
        if (!writeTextFileIfChanged(domainCMakePath, buildDomainCMakeContent(domain, headerFiles, sourceFiles), &errorMessage)) {
            emit logMessage(QString("Error writing domain CMake file %1: %2").arg(domainCMakePath, errorMessage));
            return false;
        }
    }

    const QString rootCMakePath = rootDirectory.filePath("CMakeLists.txt");
    if (!writeTextFileIfChanged(rootCMakePath, buildRootCMakeContent(domains), &errorMessage)) {
        emit logMessage(QString("Error writing root CMake file %1: %2").arg(rootCMakePath, errorMessage));
        return false;
    }

    const QString configTemplatePath = QDir(cmakeDirectoryPath).filePath("ArchitectGenLibrariesConfig.cmake.in");
    if (!writeTextFileIfChanged(configTemplatePath, buildPackageConfigTemplateContent(), &errorMessage)) {
        emit logMessage(QString("Error writing package config template %1: %2").arg(configTemplatePath, errorMessage));
        return false;
    }

    const QString readmePath = rootDirectory.filePath("README.md");
    if (!writeTextFileIfChanged(readmePath, buildReadmeContent(domains), &errorMessage)) {
        emit logMessage(QString("Error writing library README %1: %2").arg(readmePath, errorMessage));
        return false;
    }

    emit logMessage("Updated reusable library project files.");
    return true;
}