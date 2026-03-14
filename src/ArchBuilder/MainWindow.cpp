#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "GeneratorEngine.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QRegularExpression>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QDebug>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QScreen>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>

namespace {
const char kProjectMetadataFileName[] = "architectgen.project.json";

const QStringList kCommonTypes = {
    "void",
    "bool",
    "int",
    "float",
    "double",
    "QString",
    "QByteArray",
    "QVariant",
    "QList<QString>",
    "QObject*",
    "QWidget*"
};

const QStringList kAccessLevels = {"public", "protected", "private"};
const QStringList kYesNoOptions = {"No", "Yes"};
const QRegularExpression kIdentifierPattern(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));

constexpr int RoleName = Qt::UserRole + 1;
constexpr int RoleType = Qt::UserRole + 2;
constexpr int RoleDomain = Qt::UserRole + 3;
constexpr int RoleFunctions = Qt::UserRole + 4;
constexpr int RoleBaseClass = Qt::UserRole + 5;
constexpr int RoleMembers = Qt::UserRole + 6;
constexpr int RoleFunctionParameters = Qt::UserRole + 20;

class ComboBoxItemDelegate final : public QStyledItemDelegate
{
public:
    ComboBoxItemDelegate(QStringList options, bool editable, QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
        , m_options(std::move(options))
        , m_editable(editable)
    {
    }

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        auto *combo = new QComboBox(parent);
        combo->setEditable(m_editable);
        combo->addItems(m_options);
        combo->setInsertPolicy(QComboBox::NoInsert);
        combo->setFrame(false);
        return combo;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        auto *combo = qobject_cast<QComboBox *>(editor);
        if (!combo) {
            return;
        }

        const QString value = index.data(Qt::EditRole).toString();
        const int optionIndex = combo->findText(value);
        if (optionIndex >= 0) {
            combo->setCurrentIndex(optionIndex);
        } else {
            combo->setCurrentText(value);
        }
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override
    {
        auto *combo = qobject_cast<QComboBox *>(editor);
        if (!combo) {
            return;
        }

        model->setData(index, combo->currentText(), Qt::EditRole);
    }

private:
    QStringList m_options;
    bool m_editable;
};

bool yesNoToBool(const QString &value)
{
    return value.trimmed().compare("Yes", Qt::CaseInsensitive) == 0;
}

QString boolToYesNo(bool value)
{
    return value ? "Yes" : "No";
}

QString normalizeAccessSpecifier(const QString &value, const QString &fallback = "private")
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == "public" || normalized == "protected" || normalized == "private") {
        return normalized;
    }
    return fallback;
}

QJsonArray jsonArrayFromString(const QString &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    return doc.isArray() ? doc.array() : QJsonArray{};
}

QJsonObject jsonObjectFromString(const QString &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    return doc.isObject() ? doc.object() : QJsonObject{};
}

QString parameterSummary(const QList<ParamMeta> &parameters)
{
    QStringList items;
    for (const auto &param : parameters) {
        QString item = param.type;
        if (param.isConstRef) {
            item += " const &";
        } else if (!item.endsWith('*') && !item.endsWith('&')) {
            item += " ";
        }
        item += param.name;
        if (!param.defaultValue.isEmpty()) {
            item += " = " + param.defaultValue;
        }
        items << item.trimmed();
    }
    return items.join(", ");
}

QString parameterSummary(const QJsonArray &parameters)
{
    QList<ParamMeta> metas;
    for (const auto &value : parameters) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        ParamMeta param;
        param.type = object.value("type").toString();
        param.name = object.value("name").toString();
        param.defaultValue = object.value("defaultValue").toString();
        param.isConstRef = object.value("isConstRef").toBool();
        metas.append(param);
    }
    return parameterSummary(metas);
}

QJsonArray parameterArrayFromMetaList(const QList<ParamMeta> &parameters)
{
    QJsonArray array;
    for (const auto &param : parameters) {
        QJsonObject object;
        object["type"] = param.type;
        object["name"] = param.name;
        object["defaultValue"] = param.defaultValue;
        object["isConstRef"] = param.isConstRef;
        array.append(object);
    }
    return array;
}

QList<ParamMeta> parameterMetaListFromArray(const QJsonArray &parameters)
{
    QList<ParamMeta> metas;
    for (const auto &value : parameters) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        ParamMeta param;
        param.type = object.value("type").toString();
        param.name = object.value("name").toString();
        param.defaultValue = object.value("defaultValue").toString();
        param.isConstRef = object.value("isConstRef").toBool();
        metas.append(param);
    }
    return metas;
}

QList<ParamMeta> parameterMetaListFromString(const QString &paramsStr)
{
    QList<ParamMeta> parameters;
    for (const QString &part : paramsStr.split(',', Qt::SkipEmptyParts)) {
        const QString trimmed = part.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }

        QString declaration = trimmed;
        QString defaultValue;
        const int equalIndex = declaration.indexOf('=');
        if (equalIndex >= 0) {
            defaultValue = declaration.mid(equalIndex + 1).trimmed();
            declaration = declaration.left(equalIndex).trimmed();
        }

        const int lastSpace = declaration.lastIndexOf(' ');
        if (lastSpace <= 0) {
            continue;
        }

        ParamMeta param;
        param.type = declaration.left(lastSpace).trimmed();
        param.name = declaration.mid(lastSpace + 1).trimmed();
        param.defaultValue = defaultValue;
        param.isConstRef = param.type.startsWith("const ") && param.type.endsWith('&');
        if (param.isConstRef) {
            param.type = param.type.mid(QString("const ").size());
            param.type.chop(1);
            param.type = param.type.trimmed();
        }
        if (!param.type.isEmpty() && !param.name.isEmpty()) {
            parameters.append(param);
        }
    }
    return parameters;
}

FunctionMeta functionMetaFromObject(const QJsonObject &object)
{
    FunctionMeta meta;
    meta.name = object.value("name").toString();
    meta.returnType = object.value("returnType").toString();
    meta.access = normalizeAccessSpecifier(object.value("access").toString(), "public");
    meta.isConst = object.value("isConst").toBool();
    meta.isStatic = object.value("isStatic").toBool();
    meta.isVirtual = object.value("isVirtual").toBool();

    const QJsonArray parameters = object.value("parameters").toArray();
    for (const auto &value : parameters) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject parameterObject = value.toObject();
        ParamMeta parameter;
        parameter.type = parameterObject.value("type").toString();
        parameter.name = parameterObject.value("name").toString();
        parameter.defaultValue = parameterObject.value("defaultValue").toString();
        parameter.isConstRef = parameterObject.value("isConstRef").toBool();
        meta.parameters.append(parameter);
    }

    return meta;
}

MemberMeta memberMetaFromObject(const QJsonObject &object)
{
    MemberMeta meta;
    meta.type = object.value("type").toString();
    meta.name = object.value("name").toString();
    meta.defaultValue = object.value("defaultValue").toString();
    meta.access = normalizeAccessSpecifier(object.value("access").toString(), "private");
    meta.isStatic = object.value("isStatic").toBool();
    meta.isConstexpr = object.value("isConstexpr").toBool();
    return meta;
}

QJsonObject functionObjectFromMeta(const FunctionMeta &meta)
{
    QJsonObject object;
    object["name"] = meta.name;
    object["returnType"] = meta.returnType;
    object["access"] = normalizeAccessSpecifier(meta.access, "public");
    object["isConst"] = meta.isConst;
    object["isStatic"] = meta.isStatic;
    object["isVirtual"] = meta.isVirtual;

    QJsonArray parameters;
    for (const auto &param : meta.parameters) {
        QJsonObject parameterObject;
        parameterObject["type"] = param.type;
        parameterObject["name"] = param.name;
        parameterObject["defaultValue"] = param.defaultValue;
        parameterObject["isConstRef"] = param.isConstRef;
        parameters.append(parameterObject);
    }
    object["parameters"] = parameters;
    return object;
}

QJsonObject memberObjectFromMeta(const MemberMeta &meta)
{
    QJsonObject object;
    object["type"] = meta.type;
    object["name"] = meta.name;
    object["defaultValue"] = meta.defaultValue;
    object["access"] = normalizeAccessSpecifier(meta.access, "private");
    object["isStatic"] = meta.isStatic;
    object["isConstexpr"] = meta.isConstexpr;
    return object;
}

QString functionKey(const QJsonObject &object)
{
    return object.value("name").toString() + "|" + object.value("returnType").toString() + "|" + parameterSummary(object.value("parameters").toArray());
}

QStandardItem *resolveClassItem(QStandardItem *item)
{
    if (!item) {
        return nullptr;
    }

    const QString itemType = item->data(RoleType).toString();
    if (itemType == "Function") {
        return item->parent();
    }

    return itemType == "Class" ? item : nullptr;
}

QJsonArray collectFunctionObjects(QStandardItem *classItem)
{
    QJsonArray functions = jsonArrayFromString(classItem->data(RoleFunctions).toString());
    QSet<QString> knownFunctions;
    for (const auto &value : functions) {
        if (value.isObject()) {
            knownFunctions.insert(functionKey(value.toObject()));
        }
    }

    for (int row = 0; row < classItem->rowCount(); ++row) {
        QStandardItem *child = classItem->child(row);
        if (!child || child->data(RoleType).toString() != "Function") {
            continue;
        }

        const QJsonObject object = jsonObjectFromString(child->data(RoleFunctions).toString());
        if (object.isEmpty()) {
            continue;
        }

        const QString key = functionKey(object);
        if (knownFunctions.contains(key)) {
            continue;
        }

        knownFunctions.insert(key);
        functions.append(object);
    }

    return functions;
}

ClassMeta classMetaFromItem(QStandardItem *item)
{
    ClassMeta meta;
    QStandardItem *classItem = resolveClassItem(item);
    if (!classItem) {
        return meta;
    }

    meta.className = classItem->data(RoleName).toString();
    meta.domainKey = classItem->data(RoleDomain).toString();
    meta.baseClass = classItem->data(RoleBaseClass).toString();
    if (meta.baseClass.isEmpty()) {
        meta.baseClass = "QObject";
    }

    const QJsonArray functions = collectFunctionObjects(classItem);
    for (const auto &value : functions) {
        if (value.isObject()) {
            meta.functions.append(functionMetaFromObject(value.toObject()));
        }
    }

    const QJsonArray members = jsonArrayFromString(classItem->data(RoleMembers).toString());
    for (const auto &value : members) {
        if (value.isObject()) {
            meta.members.append(memberMetaFromObject(value.toObject()));
        }
    }

    return meta;
}

QString functionItemText(const FunctionMeta &meta)
{
    return QString("%1(%2) : %3").arg(meta.name, parameterSummary(meta.parameters), meta.returnType);
}

QJsonObject classMetaToJsonObject(const ClassMeta &meta)
{
    QJsonObject object;
    object["className"] = meta.className;
    object["domainKey"] = meta.domainKey;
    object["baseClass"] = meta.baseClass;

    QJsonArray functions;
    for (const auto &function : meta.functions) {
        functions.append(functionObjectFromMeta(function));
    }
    object["functions"] = functions;

    QJsonArray members;
    for (const auto &member : meta.members) {
        members.append(memberObjectFromMeta(member));
    }
    object["members"] = members;

    return object;
}

ClassMeta classMetaFromJsonObject(const QJsonObject &object)
{
    ClassMeta meta;
    meta.className = object.value("className").toString();
    meta.domainKey = object.value("domainKey").toString();
    meta.baseClass = object.value("baseClass").toString();
    if (meta.baseClass.isEmpty()) {
        meta.baseClass = "QObject";
    }

    for (const auto &value : object.value("functions").toArray()) {
        if (value.isObject()) {
            meta.functions.append(functionMetaFromObject(value.toObject()));
        }
    }

    for (const auto &value : object.value("members").toArray()) {
        if (value.isObject()) {
            meta.members.append(memberMetaFromObject(value.toObject()));
        }
    }

    return meta;
}

int findMatchingScopeBrace(const QString &content, int openBraceIndex)
{
    if (openBraceIndex < 0 || openBraceIndex >= content.size() || content.at(openBraceIndex) != '{') {
        return -1;
    }

    int depth = 0;
    for (int index = openBraceIndex; index < content.size(); ++index) {
        const QChar ch = content.at(index);
        if (ch == '{') {
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0) {
                return index;
            }
        }
    }

    return -1;
}

bool isManagedGeneratedClassFileSet(const QString &headerContent, const QString &sourceContent, const QString &domainKey)
{
    return headerContent.contains(QString("@domain %1").arg(domainKey))
        && (sourceContent.contains("// --- Generated Implementations ---")
            || sourceContent.contains("// --- Generated Stubs ---"));
}

bool parseManagedFunctionDeclaration(const QString &declarationText,
                                    const QString &className,
                                    const QString &access,
                                    FunctionMeta *function)
{
    if (!function) {
        return false;
    }

    QString declaration = declarationText.trimmed();
    if (!declaration.endsWith(';')) {
        return false;
    }
    declaration.chop(1);

    bool isStatic = false;
    bool isVirtual = false;
    if (declaration.startsWith("static ")) {
        isStatic = true;
        declaration.remove(0, QString("static ").size());
    }
    if (declaration.startsWith("virtual ")) {
        isVirtual = true;
        declaration.remove(0, QString("virtual ").size());
    }

    bool isConst = false;
    if (declaration.endsWith(" const")) {
        isConst = true;
        declaration.chop(QString(" const").size());
        declaration = declaration.trimmed();
    }

    const int openParen = declaration.indexOf('(');
    const int closeParen = declaration.lastIndexOf(')');
    if (openParen <= 0 || closeParen <= openParen) {
        return false;
    }

    const QString beforeParams = declaration.left(openParen).trimmed();
    const QString parameterText = declaration.mid(openParen + 1, closeParen - openParen - 1).trimmed();
    const int separator = beforeParams.lastIndexOf(' ');
    if (separator <= 0) {
        return false;
    }

    const QString returnType = beforeParams.left(separator).trimmed();
    const QString functionName = beforeParams.mid(separator + 1).trimmed();
    if (functionName.isEmpty() || functionName == className || functionName == ("~" + className)) {
        return false;
    }

    function->name = functionName;
    function->returnType = returnType;
    function->parameters = parameterMetaListFromString(parameterText);
    function->access = access;
    function->isConst = isConst;
    function->isStatic = isStatic;
    function->isVirtual = isVirtual;
    return true;
}

bool parseManagedMemberDeclaration(const QString &declarationText,
                                  const QString &access,
                                  MemberMeta *member)
{
    if (!member) {
        return false;
    }

    QString declaration = declarationText.trimmed();
    if (!declaration.endsWith(';')) {
        return false;
    }
    declaration.chop(1);

    bool isConstexpr = false;
    bool isStatic = false;
    if (declaration.startsWith("static constexpr ")) {
        isConstexpr = true;
        isStatic = true;
        declaration.remove(0, QString("static constexpr ").size());
    } else if (declaration.startsWith("static ")) {
        isStatic = true;
        declaration.remove(0, QString("static ").size());
    }

    QString defaultValue;
    const int equalIndex = declaration.indexOf('=');
    if (equalIndex >= 0) {
        defaultValue = declaration.mid(equalIndex + 1).trimmed();
        declaration = declaration.left(equalIndex).trimmed();
    }

    const int separator = declaration.lastIndexOf(' ');
    if (separator <= 0) {
        return false;
    }

    QString type = declaration.left(separator).trimmed();
    QString name = declaration.mid(separator + 1).trimmed();
    while (!name.isEmpty() && (name.startsWith('*') || name.startsWith('&'))) {
        type += name.left(1);
        name.remove(0, 1);
    }
    if (type.isEmpty() || name.isEmpty()) {
        return false;
    }

    member->type = type;
    member->name = name;
    member->defaultValue = defaultValue;
    member->access = access;
    member->isStatic = isStatic;
    member->isConstexpr = isConstexpr;
    return true;
}

ClassMeta parseManagedClassMetaFromFiles(const QString &headerContent, const QString &domainKey)
{
    ClassMeta meta;

    QRegularExpression classRegex("class\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*(?:final\\s*)?:\\s*public\\s+([A-Za-z_][A-Za-z0-9_:<>*&]*)");
    const QRegularExpressionMatch classMatch = classRegex.match(headerContent);
    if (!classMatch.hasMatch()) {
        return meta;
    }

    meta.className = classMatch.captured(1).trimmed();
    meta.baseClass = classMatch.captured(2).trimmed();
    meta.domainKey = domainKey;
    if (meta.baseClass.isEmpty()) {
        meta.baseClass = "QObject";
    }

    const int bodyStart = headerContent.indexOf('{', classMatch.capturedEnd(0));
    const int bodyEnd = findMatchingScopeBrace(headerContent, bodyStart);
    if (bodyStart < 0 || bodyEnd <= bodyStart) {
        return meta;
    }

    const QString body = headerContent.mid(bodyStart + 1, bodyEnd - bodyStart - 1);
    QString currentAccess = "private";
    const QStringList lines = body.split('\n');
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty() || line == "Q_OBJECT" || line.startsWith("//") || line.startsWith("/*") || line.startsWith('*')) {
            continue;
        }

        if (line == "public:" || line == "protected:" || line == "private:") {
            currentAccess = line.left(line.size() - 1);
            continue;
        }

        if (!line.endsWith(';')) {
            continue;
        }

        if (line.contains('(') && line.contains(')')) {
            FunctionMeta function;
            if (parseManagedFunctionDeclaration(line, meta.className, currentAccess, &function)) {
                meta.functions.append(function);
            }
            continue;
        }

        MemberMeta member;
        if (parseManagedMemberDeclaration(line, currentAccess, &member)) {
            meta.members.append(member);
        }
    }

    return meta;
}

QStandardItem *findDomainItem(QStandardItemModel *model, const QString &domainKey)
{
    for (int row = 0; row < model->rowCount(); ++row) {
        QStandardItem *item = model->item(row);
        if (item && item->data(RoleType).toString() == "Domain" && item->data(RoleName).toString() == domainKey) {
            return item;
        }
    }

    return nullptr;
}

QTableWidgetItem *createEditableItem(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    return item;
}

QTableWidgetItem *createReadOnlyItem(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

void setItemValidationState(QTableWidgetItem *item, bool valid, const QString &message = QString())
{
    if (!item) {
        return;
    }

    if (valid) {
        item->setBackground(Qt::transparent);
        item->setForeground(QBrush());
        item->setToolTip(QString());
        return;
    }

    item->setBackground(QColor("#fde8e8"));
    item->setForeground(QBrush(QColor("#8f2d2d")));
    item->setToolTip(message);
}

void setLineEditValidationState(QLineEdit *lineEdit, bool valid, const QString &message = QString())
{
    if (!lineEdit) {
        return;
    }

    lineEdit->setProperty("invalid", !valid);
    lineEdit->setToolTip(valid ? QString() : message);
    lineEdit->style()->unpolish(lineEdit);
    lineEdit->style()->polish(lineEdit);
    lineEdit->update();
}

bool isValidCppIdentifier(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    if (trimmed.startsWith('~')) {
        return kIdentifierPattern.match(trimmed.mid(1)).hasMatch();
    }

    return kIdentifierPattern.match(trimmed).hasMatch();
}

bool isLikelyValidCppType(const QString &value, bool allowVoid)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    if (!allowVoid && trimmed == "void") {
        return false;
    }

    int angleDepth = 0;
    for (const QChar ch : trimmed) {
        if (ch == '<') {
            ++angleDepth;
            continue;
        }
        if (ch == '>') {
            --angleDepth;
            if (angleDepth < 0) {
                return false;
            }
            continue;
        }

        if (ch.isLetterOrNumber() || ch == '_' || ch == ':' || ch == '*' || ch == '&' || ch == ',' || ch == ' ' || ch == '(' || ch == ')') {
            continue;
        }

        return false;
    }

    if (angleDepth != 0) {
        return false;
    }

    const QString normalized = trimmed;
    return normalized.contains(QRegularExpression(QStringLiteral("[A-Za-z_]")));
}

QString defaultValueForType(QString type)
{
    type = type.trimmed();
    type.remove("const ");
    type.remove('&');
    type = type.trimmed();

    if (type.endsWith('*')) {
        return "nullptr";
    }

    const QString normalized = type.toLower();
    if (normalized == "bool") {
        return "false";
    }
    if (normalized == "float" || normalized == "double") {
        return "0.0";
    }
    if (normalized == "int" || normalized == "short" || normalized == "long" || normalized == "long long"
        || normalized == "unsigned" || normalized == "unsigned int" || normalized == "unsigned short"
        || normalized == "unsigned long" || normalized == "unsigned long long" || normalized == "size_t") {
        return "0";
    }
    if (type == "QString") {
        return "QString()";
    }
    if (type == "QByteArray") {
        return "QByteArray()";
    }
    if (type == "QVariant") {
        return "QVariant()";
    }
    if (type.startsWith("QList<") || type.startsWith("QVector<") || type.startsWith("std::vector<")) {
        return "{}";
    }

    return "{}";
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_model(nullptr)
    , m_engine(new GeneratorEngine(this))
    , m_contextMenu(nullptr)
    , m_currentClassItem(nullptr)
    , m_autoSaveTimer(nullptr)
{
    ui->setupUi(this);
    configureAdaptiveWindow();
    configureWorkspaceLayout();
    configureVisualStyle();

    // 初始化 Model
    m_model = new QStandardItemModel(this);
    m_model->setHorizontalHeaderLabels({"Architecture View"});
    ui->treeView->setModel(m_model);
    
    // 设置树视图支持右键菜单
    ui->treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->treeView->setSelectionMode(QAbstractItemView::SingleSelection);

    // 创建右键菜单
    m_contextMenu = new QMenu(this);
    QAction *addClassAction = new QAction("Add New Class", this);
    connect(addClassAction, &QAction::triggered, this, &MainWindow::onAddNewClassFromContextMenu);
    m_contextMenu->addAction(addClassAction);

    // 初始化自动保存定时器（5分钟自动保存一次）
    m_autoSaveTimer = new QTimer(this);
    m_autoSaveTimer->setInterval(5 * 60 * 1000); // 5分钟
    connect(m_autoSaveTimer, &QTimer::timeout, this, [this]() {
        if (persistProjectModel()) {
            statusBar()->showMessage("Auto-saved", 2000);
        }
    });
    m_autoSaveTimer->start();
    
    // 连接信号
    connect(ui->treeView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::onTreeSelectionChanged);
    connect(ui->treeView, &QTreeView::customContextMenuRequested,
            this, &MainWindow::onCustomContextMenuRequested);
    connect(ui->actionNew_Class, &QAction::triggered, this, &MainWindow::onActionNewClass);
    connect(ui->actionNew_Function, &QAction::triggered, this, &MainWindow::onActionNewFunction);
    connect(ui->actionSave_Template, &QAction::triggered, this, &MainWindow::onActionSaveTemplate);
    connect(ui->actionGenerate_Code, &QAction::triggered, this, &MainWindow::onActionGenerate);
    connect(ui->actionGenerate_All, &QAction::triggered, this, &MainWindow::onActionGenerateAll);
    connect(ui->actionOpen_Project, &QAction::triggered, this, &MainWindow::onActionOpenProject);
    connect(ui->actionExit, &QAction::triggered, this, &QApplication::quit);
    connect(ui->actionAbout, &QAction::triggered, this, &MainWindow::onActionAbout);
    
    // 连接属性面板按钮
    connect(ui->addFunctionBtn, &QPushButton::clicked, this, &MainWindow::onAddFunctionClicked);
    connect(ui->addMemberBtn, &QPushButton::clicked, this, &MainWindow::onAddMemberClicked);
    connect(ui->deleteItemBtn, &QPushButton::clicked, this, &MainWindow::onDeleteItemClicked);
    connect(ui->saveClassBtn, &QPushButton::clicked, this, &MainWindow::onSaveClassClicked);
    connect(ui->functionsTable, &QTableWidget::itemSelectionChanged, this, &MainWindow::onFunctionSelectionChanged);
    connect(ui->addParameterBtn, &QPushButton::clicked, this, &MainWindow::onAddParameterClicked);
    connect(ui->deleteParameterBtn, &QPushButton::clicked, this, &MainWindow::onDeleteParameterClicked);
    connect(ui->funcConstCheck, &QCheckBox::toggled, this, &MainWindow::onFunctionFlagsChanged);
    connect(ui->funcStaticCheck, &QCheckBox::toggled, this, &MainWindow::onFunctionFlagsChanged);
    connect(ui->funcVirtualCheck, &QCheckBox::toggled, this, &MainWindow::onFunctionFlagsChanged);
    connect(ui->funcAccessCombo, &QComboBox::currentTextChanged, this, &MainWindow::onFunctionFlagsChanged);
    connect(ui->membersTable, &QTableWidget::itemSelectionChanged, this, &MainWindow::onMemberSelectionChanged);
    connect(ui->memberAccessCombo, &QComboBox::currentTextChanged, this, &MainWindow::onMemberFlagsChanged);
    connect(ui->memberStaticCheck, &QCheckBox::toggled, this, &MainWindow::onMemberFlagsChanged);
    connect(ui->memberConstexprCheck, &QCheckBox::toggled, this, &MainWindow::onMemberFlagsChanged);
    connect(ui->editorTabs, &QTabWidget::currentChanged, this, &MainWindow::onEditorTabChanged);
    connect(ui->classNameEdit, &QLineEdit::textChanged, this, [this] {
        refreshPreview();
        updateValidationFeedback();
    });
    connect(ui->baseClassEdit, &QLineEdit::textChanged, this, [this] {
        refreshPreview();
        updateValidationFeedback();
    });
    connect(ui->functionsTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (!item) {
            return;
        }

        if (item->column() == 2) {
            QList<ParamMeta> parameters = parameterMetaListFromString(item->text());
            if (QTableWidgetItem *nameItem = ui->functionsTable->item(item->row(), 0)) {
                nameItem->setData(RoleFunctionParameters, QJsonDocument(parameterArrayFromMetaList(parameters)).toJson(QJsonDocument::Compact));
            }
            if (ui->functionsTable->currentRow() == item->row()) {
                loadParameterEditorFromSelectedFunction();
            }
        }
        if ((item->column() == 3 || item->column() == 4 || item->column() == 5) && ui->functionsTable->currentRow() == item->row()) {
            loadFunctionOptionsFromSelectedFunction();
        }
        if (item->column() == 6 && ui->functionsTable->currentRow() == item->row()) {
            loadFunctionOptionsFromSelectedFunction();
        }
        refreshPreview();
        updateValidationFeedback();
        persistProjectModel();
    });
    connect(ui->membersTable, &QTableWidget::itemChanged, this, [this] {
        if (ui->membersTable->currentRow() >= 0) {
            loadMemberOptionsFromSelectedMember();
        }
        refreshPreview();
        updateValidationFeedback();
        persistProjectModel();
    });
    connect(ui->parametersTable, &QTableWidget::itemChanged, this, [this] {
        syncSelectedFunctionParameters();
        updateValidationFeedback();
    });

    m_templateRootPath = QDir(QCoreApplication::applicationDirPath() + "/../../../templates").absolutePath();
    m_projectRootPath = QDir(QCoreApplication::applicationDirPath() + "/../../../templates").absolutePath();

    QDir().mkpath(m_projectRootPath);
    updateOutputPathDisplay();

    ui->logEdit->append(QString("Template root: %1").arg(QDir::toNativeSeparators(m_templateRootPath)));
    ui->logEdit->append(QString("Output path: %1").arg(QDir::toNativeSeparators(m_projectRootPath)));

    initializeDefaultDomains();
    if (!loadProjectModel(m_projectRootPath)) {
        const bool imported = reconcileModelWithFilesystem();
        refreshTreePresentation();
        ui->treeView->expandAll();
        if (imported) {
            persistProjectModel();
            ui->logEdit->append("No metadata found. Imported managed classes from disk.");
        }
    }

    // 连接引擎日志到 UI
    connect(m_engine, &GeneratorEngine::logMessage, this, [this](const QString &msg){
        ui->logEdit->append(msg);
        qDebug() << "[GenCore]" << msg;
    });

    configureTables();

    refreshPreview();
    updateValidationFeedback();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::configureAdaptiveWindow()
{
    setMinimumSize(1180, 760);

    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        resize(1360, 880);
        return;
    }

    const QRect available = screen->availableGeometry();
    const int width = qBound(minimumWidth(), static_cast<int>(available.width() * 0.84), available.width());
    const int height = qBound(minimumHeight(), static_cast<int>(available.height() * 0.88), available.height());

    resize(width, height);
    move(available.center() - rect().center());
}

void MainWindow::configureWorkspaceLayout()
{
    ui->treeView->setMinimumWidth(240);
    ui->treeView->setMaximumWidth(360);
    ui->treeView->setHeaderHidden(true);
    ui->treeView->setUniformRowHeights(true);
    ui->treeView->setIndentation(18);
    ui->treeView->setAnimated(true);

    if (QSplitter *mainSplitter = findChild<QSplitter *>("mainSplitter")) {
        mainSplitter->setChildrenCollapsible(false);
        mainSplitter->setStretchFactor(0, 0);
        mainSplitter->setStretchFactor(1, 1);
        mainSplitter->setSizes({280, 1120});
    }

    if (QSplitter *contentSplitter = findChild<QSplitter *>("contentSplitter")) {
        contentSplitter->setChildrenCollapsible(false);
        contentSplitter->setStretchFactor(0, 3);
        contentSplitter->setStretchFactor(1, 2);
        contentSplitter->setSizes({760, 520});
    }

    if (QSplitter *inspectorSplitter = findChild<QSplitter *>("inspectorSplitter")) {
        inspectorSplitter->setChildrenCollapsible(false);
        inspectorSplitter->setStretchFactor(0, 3);
        inspectorSplitter->setStretchFactor(1, 1);
        inspectorSplitter->setSizes({520, 220});
    }

    if (QLineEdit *outputPathEdit = findChild<QLineEdit *>("outputPathEdit")) {
        outputPathEdit->setCursorPosition(0);
        outputPathEdit->setMinimumWidth(360);
    }

    if (QLabel *navigationHintLabel = findChild<QLabel *>("navigationHintLabel")) {
        navigationHintLabel->setWordWrap(true);
    }

    updateEditorActionButtons();
}

void MainWindow::configureTables()
{
    const QList<QTableWidget *> tables = {ui->functionsTable, ui->membersTable, ui->parametersTable};
    for (QTableWidget *table : tables) {
        table->setAlternatingRowColors(true);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
        table->verticalHeader()->setVisible(false);
        table->horizontalHeader()->setStretchLastSection(false);
    }

    ui->functionsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->functionsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui->functionsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    ui->functionsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    ui->functionsTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    ui->functionsTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    ui->functionsTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    ui->functionsTable->setItemDelegateForColumn(1, new ComboBoxItemDelegate(kCommonTypes, false, ui->functionsTable));
    ui->functionsTable->setItemDelegateForColumn(3, new ComboBoxItemDelegate(kYesNoOptions, false, ui->functionsTable));
    ui->functionsTable->setItemDelegateForColumn(4, new ComboBoxItemDelegate(kYesNoOptions, false, ui->functionsTable));
    ui->functionsTable->setItemDelegateForColumn(5, new ComboBoxItemDelegate(kYesNoOptions, false, ui->functionsTable));
    ui->functionsTable->setItemDelegateForColumn(6, new ComboBoxItemDelegate(kAccessLevels, false, ui->functionsTable));

    ui->membersTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->membersTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui->membersTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    ui->membersTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    ui->membersTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    ui->membersTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    ui->membersTable->setItemDelegateForColumn(0, new ComboBoxItemDelegate(kCommonTypes, false, ui->membersTable));
    ui->membersTable->setItemDelegateForColumn(3, new ComboBoxItemDelegate(kAccessLevels, false, ui->membersTable));
    ui->membersTable->setItemDelegateForColumn(4, new ComboBoxItemDelegate(kYesNoOptions, false, ui->membersTable));
    ui->membersTable->setItemDelegateForColumn(5, new ComboBoxItemDelegate(kYesNoOptions, false, ui->membersTable));

    ui->parametersTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->parametersTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui->parametersTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    ui->parametersTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    ui->parametersTable->setItemDelegateForColumn(0, new ComboBoxItemDelegate(kCommonTypes, false, ui->parametersTable));
    ui->parametersTable->setItemDelegateForColumn(3, new ComboBoxItemDelegate(kYesNoOptions, false, ui->parametersTable));
    ui->parametersTable->setColumnHidden(2, true);
}

void MainWindow::configureVisualStyle()
{
    const QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    ui->headerPreviewEdit->setFont(fixedFont);
    ui->sourcePreviewEdit->setFont(fixedFont);
    ui->logEdit->setFont(fixedFont);

    setStyleSheet(
        "QMainWindow { background: #edf1f4; }"
        "QGroupBox {"
        "  background: #ffffff;"
        "  border: 1px solid #cfd7df;"
        "  border-radius: 10px;"
        "  margin-top: 16px;"
        "  color: #1b2a37;"
        "  font-weight: 600;"
        "}"
        "QGroupBox::title { subcontrol-origin: margin; left: 14px; padding: 0 6px; }"
        "QLabel { color: #304352; }"
        "QLabel#navigationHintLabel { color: #5f7282; padding-bottom: 4px; }"
        "QLineEdit, QPlainTextEdit, QTextEdit, QTableWidget, QComboBox {"
        "  background: #fbfcfd;"
        "  border: 1px solid #c9d3dc;"
        "  border-radius: 7px;"
        "  padding: 7px 8px;"
        "  selection-background-color: #335f7d;"
        "  color: #1f2d3a;"
        "}"
        "QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QTableWidget:focus, QComboBox:focus { border-color: #335f7d; }"
        "QLineEdit[invalid=\"true\"] { border-color: #b55454; background: #fff3f3; color: #7a2525; }"
        "QTableWidget { gridline-color: #e1e7ed; alternate-background-color: #f3f6f8; }"
        "QTreeView {"
        "  background: #fbfcfd;"
        "  border: 1px solid #c9d3dc;"
        "  border-radius: 8px;"
        "  padding: 6px;"
        "  alternate-background-color: #f3f6f8;"
        "  show-decoration-selected: 1;"
        "}"
        "QTreeView::item { padding: 6px 4px; border-radius: 5px; }"
        "QTreeView::item:selected { background: #d8e3eb; color: #1c2f3c; }"
        "QHeaderView::section {"
        "  background: #e3e9ee;"
        "  color: #223545;"
        "  border: none;"
        "  border-bottom: 1px solid #d1d9e2;"
        "  padding: 9px 8px;"
        "  font-weight: 600;"
        "}"
        "QPushButton {"
        "  background: #29485d;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 7px;"
        "  padding: 9px 16px;"
        "  min-height: 20px;"
        "  font-weight: 600;"
        "}"
        "QPushButton:hover { background: #223d4f; }"
        "QPushButton#deleteItemBtn, QPushButton#deleteParameterBtn { background: #b55454; }"
        "QPushButton#deleteItemBtn:hover, QPushButton#deleteParameterBtn:hover { background: #954343; }"
        "QPushButton#saveClassBtn { background: #2c6a54; }"
        "QPushButton#saveClassBtn:hover { background: #245643; }"
        "QTabWidget::pane { border: 0; }"
        "QTabBar::tab {"
        "  background: #dde4ea;"
        "  color: #33495a;"
        "  border-radius: 7px;"
        "  padding: 9px 16px;"
        "  margin-right: 6px;"
        "}"
        "QTabBar::tab:selected { background: #29485d; color: white; }"
        "QSplitter::handle { background: #d3dbe3; }"
    );
}

void MainWindow::updateEditorActionButtons()
{
    const bool functionsTabActive = ui->editorTabs->currentIndex() == 0;
    ui->addFunctionBtn->setVisible(functionsTabActive);
    ui->addMemberBtn->setVisible(!functionsTabActive);
    ui->deleteItemBtn->setText(functionsTabActive ? "Delete Function" : "Delete Member");
}

QString MainWindow::createDefaultClassName(QStandardItem *domainItem) const
{
    if (!domainItem) {
        return "NewClass";
    }

    int counter = domainItem->rowCount() + 1;
    while (true) {
        const QString candidate = QString("NewClass%1").arg(counter);
        bool exists = false;
        for (int row = 0; row < domainItem->rowCount(); ++row) {
            QStandardItem *classItem = domainItem->child(row);
            if (classItem && classItem->data(RoleName).toString() == candidate) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            return candidate;
        }
        ++counter;
    }
}

void MainWindow::createClassUnderDomain(QStandardItem *domainItem)
{
    if (!domainItem) {
        return;
    }

    const QString className = createDefaultClassName(domainItem);
    QStandardItem *classItem = new QStandardItem(className);
    classItem->setData(className, RoleName);
    classItem->setData("Class", RoleType);
    classItem->setData(domainItem->data(RoleName).toString(), RoleDomain);
    classItem->setData("QObject", RoleBaseClass);

    const QJsonArray emptyArray;
    const QString emptyJson = QString::fromUtf8(QJsonDocument(emptyArray).toJson(QJsonDocument::Compact));
    classItem->setData(emptyJson, RoleFunctions);
    classItem->setData(emptyJson, RoleMembers);

    domainItem->appendRow(classItem);
    updateClassItemPresentation(classItem, ClassMeta{className, QString(), domainItem->data(RoleName).toString(), "QObject", {}, {}});
    ui->treeView->expand(domainItem->index());
    ui->treeView->setCurrentIndex(classItem->index());
    ui->editorTabs->setCurrentIndex(0);
    ui->classNameEdit->setFocus();
    ui->classNameEdit->selectAll();
    persistProjectModel();
}

void MainWindow::insertDefaultFunctionRow()
{
    const QSignalBlocker blocker(ui->functionsTable);
    const int row = ui->functionsTable->rowCount();
    ui->functionsTable->insertRow(row);

    auto *nameItem = createEditableItem(QString("newFunction%1").arg(row + 1));
    nameItem->setData(RoleFunctionParameters, QJsonDocument(parameterArrayFromMetaList({})).toJson(QJsonDocument::Compact));
    ui->functionsTable->setItem(row, 0, nameItem);
    ui->functionsTable->setItem(row, 1, createEditableItem("void"));
    ui->functionsTable->setItem(row, 2, createReadOnlyItem(QString()));
    ui->functionsTable->setItem(row, 3, createEditableItem("No"));
    ui->functionsTable->setItem(row, 4, createEditableItem("No"));
    ui->functionsTable->setItem(row, 5, createEditableItem("No"));
    ui->functionsTable->setItem(row, 6, createEditableItem("public"));

    ui->editorTabs->setCurrentIndex(0);
    ui->functionsTable->selectRow(row);
    ui->functionsTable->editItem(nameItem);
    loadParameterEditorFromSelectedFunction();
    loadFunctionOptionsFromSelectedFunction();
    refreshPreview();
    persistProjectModel();
}

void MainWindow::insertDefaultMemberRow()
{
    const QSignalBlocker blocker(ui->membersTable);
    const int row = ui->membersTable->rowCount();
    ui->membersTable->insertRow(row);
    ui->membersTable->setItem(row, 0, createEditableItem("int"));
    ui->membersTable->setItem(row, 1, createEditableItem(QString("m_member%1").arg(row + 1)));
    ui->membersTable->setItem(row, 2, createEditableItem(QString()));
    ui->membersTable->setItem(row, 3, createEditableItem("private"));
    ui->membersTable->setItem(row, 4, createEditableItem("No"));
    ui->membersTable->setItem(row, 5, createEditableItem("No"));

    ui->editorTabs->setCurrentIndex(1);
    ui->membersTable->selectRow(row);
    if (QTableWidgetItem *nameItem = ui->membersTable->item(row, 1)) {
        ui->membersTable->editItem(nameItem);
    }
    loadMemberOptionsFromSelectedMember();
    refreshPreview();
    persistProjectModel();
}

bool MainWindow::validateClassMeta(const ClassMeta &meta, QStandardItem *targetClassItem, QString *message) const
{
    QStringList issues;
    const QString className = meta.className.trimmed();

    if (!isValidCppIdentifier(className)) {
        issues << "Class name must be a valid C++ identifier.";
    }

    QStandardItem *domainItem = targetClassItem ? targetClassItem->parent() : nullptr;
    if (!domainItem && !meta.domainKey.isEmpty()) {
        domainItem = findDomainItem(m_model, meta.domainKey);
    }

    if (domainItem) {
        for (int row = 0; row < domainItem->rowCount(); ++row) {
            QStandardItem *sibling = domainItem->child(row);
            if (!sibling || sibling == targetClassItem || sibling->data(RoleType).toString() != "Class") {
                continue;
            }
            if (sibling->data(RoleName).toString().compare(className, Qt::CaseSensitive) == 0) {
                issues << QString("Class '%1' already exists in this domain.").arg(className);
                break;
            }
        }
    }

    QSet<QString> memberNames;
    for (const MemberMeta &member : meta.members) {
        const QString memberName = member.name.trimmed();
        if (!isLikelyValidCppType(member.type, false)) {
            issues << QString("Member '%1' has an invalid type '%2'.").arg(member.name, member.type);
        }
        if (!isValidCppIdentifier(memberName)) {
            issues << QString("Member '%1' is not a valid C++ identifier.").arg(member.name);
            continue;
        }
        if (memberNames.contains(memberName)) {
            issues << QString("Member '%1' is duplicated.").arg(memberName);
        }
        memberNames.insert(memberName);
    }

    QSet<QString> functionSignatures;
    for (const FunctionMeta &function : meta.functions) {
        const QString functionName = function.name.trimmed();
        if (!isLikelyValidCppType(function.returnType, true)) {
            issues << QString("Function '%1' has an invalid return type '%2'.").arg(function.name, function.returnType);
        }
        if (!isValidCppIdentifier(functionName)) {
            issues << QString("Function '%1' is not a valid C++ identifier.").arg(function.name);
            continue;
        }

        QStringList parameterTypes;
        QSet<QString> parameterNames;
        for (const ParamMeta &param : function.parameters) {
            const QString paramName = param.name.trimmed();
            if (!isLikelyValidCppType(param.type, false)) {
                issues << QString("Parameter '%1' in function '%2' has invalid type '%3'.").arg(param.name, functionName, param.type);
            }
            if (paramName.isEmpty()) {
                issues << QString("Function '%1' contains a parameter without a name.").arg(functionName);
            } else if (!isValidCppIdentifier(paramName)) {
                issues << QString("Parameter '%1' in function '%2' is invalid.").arg(param.name, functionName);
            }
            if (!paramName.isEmpty() && parameterNames.contains(paramName)) {
                issues << QString("Function '%1' contains duplicate parameter '%2'.").arg(functionName, paramName);
            }
            if (!paramName.isEmpty()) {
                parameterNames.insert(paramName);
            }
            parameterTypes << param.type.trimmed();
        }

        const QString signatureKey = functionName + "|" + parameterTypes.join(',');
        if (functionSignatures.contains(signatureKey)) {
            issues << QString("Function '%1' has a duplicate signature.").arg(functionName);
        }
        functionSignatures.insert(signatureKey);
    }

    issues.removeDuplicates();
    if (message) {
        *message = issues.join("\n");
    }
    return issues.isEmpty();
}

void MainWindow::updateClassItemPresentation(QStandardItem *classItem, const ClassMeta &meta)
{
    if (!classItem) {
        return;
    }

    classItem->setText(QString("%1  [F%2 M%3]").arg(meta.className).arg(meta.functions.count()).arg(meta.members.count()));
    classItem->setIcon(QIcon(QStringLiteral(":/icons/class.svg")));

    for (int row = 0; row < classItem->rowCount(); ++row) {
        QStandardItem *functionItem = classItem->child(row);
        if (!functionItem || functionItem->data(RoleType).toString() != "Function") {
            continue;
        }
        functionItem->setIcon(QIcon(QStringLiteral(":/icons/function.svg")));
    }

    updateDomainItemPresentation(classItem->parent());
}

void MainWindow::updateDomainItemPresentation(QStandardItem *domainItem)
{
    if (!domainItem) {
        return;
    }

    int classCount = 0;
    for (int row = 0; row < domainItem->rowCount(); ++row) {
        QStandardItem *classItem = domainItem->child(row);
        if (classItem && classItem->data(RoleType).toString() == "Class") {
            ++classCount;
        }
    }

    domainItem->setText(QString("%1  [%2]").arg(domainItem->data(RoleName).toString()).arg(classCount));
    domainItem->setIcon(QIcon(QStringLiteral(":/icons/domain.svg")));
}

void MainWindow::refreshTreePresentation()
{
    for (int row = 0; row < m_model->rowCount(); ++row) {
        QStandardItem *domainItem = m_model->item(row);
        if (!domainItem || domainItem->data(RoleType).toString() != "Domain") {
            continue;
        }

        updateDomainItemPresentation(domainItem);
        for (int childRow = 0; childRow < domainItem->rowCount(); ++childRow) {
            QStandardItem *classItem = domainItem->child(childRow);
            if (!classItem || classItem->data(RoleType).toString() != "Class") {
                continue;
            }
            updateClassItemPresentation(classItem, classMetaFromItem(classItem));
        }
    }
}

void MainWindow::updateValidationFeedback()
{
    setLineEditValidationState(ui->classNameEdit, true);

    for (int row = 0; row < ui->functionsTable->rowCount(); ++row) {
        for (int column = 0; column < ui->functionsTable->columnCount(); ++column) {
            setItemValidationState(ui->functionsTable->item(row, column), true);
        }
    }

    for (int row = 0; row < ui->membersTable->rowCount(); ++row) {
        for (int column = 0; column < ui->membersTable->columnCount(); ++column) {
            setItemValidationState(ui->membersTable->item(row, column), true);
        }
    }

    for (int row = 0; row < ui->parametersTable->rowCount(); ++row) {
        for (int column = 0; column < ui->parametersTable->columnCount(); ++column) {
            setItemValidationState(ui->parametersTable->item(row, column), true);
        }
    }

    if (!m_currentClassItem) {
        ui->saveClassBtn->setEnabled(false);
        statusBar()->showMessage("Select a class to edit.", 2000);
        return;
    }

    const ClassMeta meta = getClassMetaFromPanel();
    QStringList issues;

    if (!isValidCppIdentifier(meta.className.trimmed())) {
        const QString message = "Class name must be a valid C++ identifier.";
        setLineEditValidationState(ui->classNameEdit, false, message);
        issues << message;
    } else {
        QStandardItem *domainItem = m_currentClassItem->parent();
        if (domainItem) {
            for (int row = 0; row < domainItem->rowCount(); ++row) {
                QStandardItem *sibling = domainItem->child(row);
                if (!sibling || sibling == m_currentClassItem || sibling->data(RoleType).toString() != "Class") {
                    continue;
                }
                if (sibling->data(RoleName).toString() == meta.className.trimmed()) {
                    const QString message = QString("Class '%1' already exists in this domain.").arg(meta.className.trimmed());
                    setLineEditValidationState(ui->classNameEdit, false, message);
                    issues << message;
                    break;
                }
            }
        }
    }

    QHash<QString, QList<int>> functionRowsBySignature;
    for (int row = 0; row < meta.functions.count(); ++row) {
        const FunctionMeta &function = meta.functions.at(row);
        const QString functionName = function.name.trimmed();

        if (!isLikelyValidCppType(function.returnType, true)) {
            const QString message = QString("Function '%1' has an invalid return type '%2'.").arg(function.name, function.returnType);
            setItemValidationState(ui->functionsTable->item(row, 1), false, message);
            issues << message;
        }

        if (!isValidCppIdentifier(functionName)) {
            const QString message = QString("Function '%1' is not a valid C++ identifier.").arg(function.name);
            setItemValidationState(ui->functionsTable->item(row, 0), false, message);
            issues << message;
        }

        QStringList parameterTypes;
        QSet<QString> parameterNames;
        for (const ParamMeta &param : function.parameters) {
            parameterTypes << param.type.trimmed();
            const QString paramName = param.name.trimmed();
            if (!isLikelyValidCppType(param.type, false)) {
                const QString message = QString("Parameter '%1' in function '%2' has invalid type '%3'.").arg(param.name, functionName, param.type);
                issues << message;
            }
            if (!paramName.isEmpty()) {
                if (parameterNames.contains(paramName)) {
                    const QString message = QString("Function '%1' contains duplicate parameter '%2'.").arg(functionName, paramName);
                    issues << message;
                }
                parameterNames.insert(paramName);
            }
        }

        functionRowsBySignature[functionName + "|" + parameterTypes.join(',')].append(row);
    }

    for (auto it = functionRowsBySignature.cbegin(); it != functionRowsBySignature.cend(); ++it) {
        if (it.value().size() < 2) {
            continue;
        }
        for (int row : it.value()) {
            const QString message = QString("Function '%1' has a duplicate signature.").arg(ui->functionsTable->item(row, 0) ? ui->functionsTable->item(row, 0)->text() : QString());
            setItemValidationState(ui->functionsTable->item(row, 0), false, message);
            issues << message;
        }
    }

    QHash<QString, QList<int>> memberRowsByName;
    for (int row = 0; row < meta.members.count(); ++row) {
        const QString memberName = meta.members.at(row).name.trimmed();
        if (!isLikelyValidCppType(meta.members.at(row).type, false)) {
            const QString message = QString("Member '%1' has an invalid type '%2'.").arg(meta.members.at(row).name, meta.members.at(row).type);
            setItemValidationState(ui->membersTable->item(row, 0), false, message);
            issues << message;
        }
        if (!isValidCppIdentifier(memberName)) {
            const QString message = QString("Member '%1' is not a valid C++ identifier.").arg(meta.members.at(row).name);
            setItemValidationState(ui->membersTable->item(row, 1), false, message);
            issues << message;
        }
        memberRowsByName[memberName].append(row);
    }

    for (auto it = memberRowsByName.cbegin(); it != memberRowsByName.cend(); ++it) {
        if (it.key().isEmpty() || it.value().size() < 2) {
            continue;
        }
        for (int row : it.value()) {
            const QString message = QString("Member '%1' is duplicated.").arg(it.key());
            setItemValidationState(ui->membersTable->item(row, 1), false, message);
            issues << message;
        }
    }

    const int functionRow = ui->functionsTable->currentRow();
    if (functionRow >= 0 && functionRow < meta.functions.count()) {
        QHash<QString, QList<int>> parameterRowsByName;
        const QList<ParamMeta> parameters = meta.functions.at(functionRow).parameters;
        for (int row = 0; row < parameters.count(); ++row) {
            const QString paramName = parameters.at(row).name.trimmed();
            if (!isLikelyValidCppType(parameters.at(row).type, false)) {
                const QString message = QString("Parameter '%1' in function '%2' has invalid type '%3'.").arg(parameters.at(row).name, meta.functions.at(functionRow).name, parameters.at(row).type);
                setItemValidationState(ui->parametersTable->item(row, 0), false, message);
                issues << message;
            }
            if (paramName.isEmpty()) {
                const QString message = QString("Function '%1' contains a parameter without a name.").arg(meta.functions.at(functionRow).name);
                setItemValidationState(ui->parametersTable->item(row, 1), false, message);
                issues << message;
                continue;
            }
            if (!isValidCppIdentifier(paramName)) {
                const QString message = QString("Parameter '%1' in function '%2' is invalid.").arg(parameters.at(row).name, meta.functions.at(functionRow).name);
                setItemValidationState(ui->parametersTable->item(row, 1), false, message);
                issues << message;
            }
            parameterRowsByName[paramName].append(row);
        }

        for (auto it = parameterRowsByName.cbegin(); it != parameterRowsByName.cend(); ++it) {
            if (it.key().isEmpty() || it.value().size() < 2) {
                continue;
            }
            for (int row : it.value()) {
                const QString message = QString("Function '%1' contains duplicate parameter '%2'.").arg(meta.functions.at(functionRow).name, it.key());
                setItemValidationState(ui->parametersTable->item(row, 1), false, message);
                issues << message;
            }
        }
    }

    issues.removeDuplicates();
    const bool valid = issues.isEmpty();
    ui->saveClassBtn->setEnabled(valid);
    statusBar()->showMessage(valid ? QString("Editing %1").arg(meta.className.trimmed().isEmpty() ? QString("class") : meta.className.trimmed())
                                   : QString("Validation: %1").arg(issues.first()),
                             3000);
}

void MainWindow::initializeDefaultDomains()
{
    m_model->clear();
    m_model->setHorizontalHeaderLabels({"Project Structure"});

    QStringList domains = {
        "Core_Business",
        "Hardware_Abstraction",
        "Data_Persistence",
        "User_Interaction",
        "System_Infrastructure",
        "External_Integration"
    };

    for (const QString &domain : domains) {
        QStandardItem *item = new QStandardItem(domain);
        item->setData(domain, Qt::UserRole + 1); // 存储 DomainKey
        item->setData("Domain", Qt::UserRole + 2); // 标记类型
        m_model->appendRow(item);
    }

    refreshTreePresentation();
}

void MainWindow::updateOutputPathDisplay()
{
    if (QLineEdit *outputPathEdit = findChild<QLineEdit *>("outputPathEdit")) {
        outputPathEdit->setText(QDir::toNativeSeparators(m_projectRootPath));
    }
    statusBar()->showMessage(QString("Output directory: %1").arg(QDir::toNativeSeparators(m_projectRootPath)));
}

void MainWindow::onCustomContextMenuRequested(const QPoint &point)
{
    QModelIndex index = ui->treeView->currentIndex();
    if (!index.isValid()) return;
    
    QStandardItem *item = m_model->itemFromIndex(index);
    if (!item) return;
    
    QString itemType = item->data(Qt::UserRole + 2).toString();
    if (itemType == "Domain" || itemType == "Class") {
        m_contextMenu->exec(ui->treeView->mapToGlobal(point));
    }
}

void MainWindow::onAddNewClassFromContextMenu()
{
    QModelIndex index = ui->treeView->currentIndex();
    if (!index.isValid()) return;
    
    QStandardItem *item = m_model->itemFromIndex(index);
    if (!item) return;
    
    // 确保是Class节点，获取其父节点（Domain）
    QStandardItem *domainItem = item;
    if (item->data(Qt::UserRole + 2).toString() == "Class") {
        domainItem = item->parent();
    }
    
    if (!domainItem) return;

    createClassUnderDomain(domainItem);
}

void MainWindow::onActionNewClass()
{
    QModelIndex index = ui->treeView->currentIndex();
    if (!index.isValid()) {
        QMessageBox::warning(this, "Tip", "Please select a Domain folder first.");
        return;
    }

    QStandardItem *parentItem = m_model->itemFromIndex(index);
    // 确保选中的是 Domain 节点 (第一层)
    if (parentItem->parent() != nullptr) {
        parentItem = parentItem->parent();
    }

    createClassUnderDomain(parentItem);
}

void MainWindow::onActionNewFunction()
{
    QStandardItem *classItem = resolveClassItem(m_model->itemFromIndex(ui->treeView->currentIndex()));
    if (!classItem) {
        QMessageBox::warning(this, "Tip", "Please select a Class to add function.");
        return;
    }

    if (m_currentClassItem && m_currentClassItem == classItem) {
        saveClassInfoToNode(classItem, getClassMetaFromPanel());
    }

    m_currentClassItem = classItem;
    updateClassInfoPanel(classItem);
    insertDefaultFunctionRow();
}

void MainWindow::onActionSaveTemplate()
{
    if (m_currentClassItem) {
        saveClassInfoToNode(m_currentClassItem, getClassMetaFromPanel());
    }

    ClassMeta meta = getCurrentClassMeta();
    if (meta.className.isEmpty()) {
        QMessageBox::warning(this, "Tip", "Please select a Class to save as template.");
        return;
    }

    QString tplDir = QDir(m_templateRootPath).filePath(meta.domainKey);
    QDir d(tplDir);
    if (!d.exists()) {
        if (!d.mkpath(".")) {
            QMessageBox::critical(this, "Error", "Cannot create template directory: " + tplDir);
            return;
        }
    }

    // header template
    QString hTplPath = d.filePath("class.h.tpl");
    QFile hF(hTplPath);
    if (hF.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream ts(&hF);
        ts << "#ifndef {{INCLUDE_GUARD}}\n";
        ts << "#define {{INCLUDE_GUARD}}\n\n";
        ts << "#include <QObject>\n";
        ts << "{{BASE_CLASS_INCLUDE}}\n";
        ts << "class {{CLASS_NAME}} : public {{BASE_CLASS}}\n";
        ts << "{\n{{CLASS_BODY}}};\n\n";
        ts << "#endif // {{INCLUDE_GUARD}}\n";
        hF.close();
    } else {
        QMessageBox::warning(this, "Warning", "Cannot write header template: " + hTplPath);
    }

    // source template
    QString cppTplPath = d.filePath("class.cpp.tpl");
    QFile cF(cppTplPath);
    if (cF.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream ts(&cF);
        ts << "#include \"{{CLASS_NAME}}.h\"\n\n";
        ts << "{{CLASS_NAME}}::{{CLASS_NAME}}({{PARENT_TYPE}}parent)\n    : {{BASE_CLASS}}(parent)\n{\n}\n\n";
        ts << "{{CLASS_NAME}}::~{{CLASS_NAME}}()\n{\n}\n\n";
        ts << "// --- Generated Implementations ---\n";
        ts << "{{FUNCTION_IMPLEMENTATIONS}}\n";
        cF.close();
    } else {
        QMessageBox::warning(this, "Warning", "Cannot write source template: " + cppTplPath);
    }

    QMessageBox::information(this, "Saved", "Templates saved to: " + d.absolutePath());
    refreshPreview();
    persistProjectModel();
}

void MainWindow::onActionGenerate()
{
    QModelIndex index = ui->treeView->currentIndex();
    QStandardItem *classItem = index.isValid() ? resolveClassItem(m_model->itemFromIndex(index)) : nullptr;
    if (classItem && classItem == m_currentClassItem) {
        saveClassInfoToNode(classItem, getClassMetaFromPanel());
    }

    ClassMeta meta = getCurrentClassMeta();
    if (meta.className.isEmpty()) {
        QMessageBox::warning(this, "Error", "No valid class selected.");
        return;
    }

    QString validationMessage;
    if (!validateClassMeta(meta, classItem, &validationMessage)) {
        QMessageBox::warning(this, "Invalid Class Definition", validationMessage);
        return;
    }

        bool success = m_engine->generateClass(meta, m_projectRootPath);
    if (success) {
        persistProjectModel();
        QMessageBox::information(this, "Success",
                                 "Code generated successfully in:\n" + QDir(m_projectRootPath).filePath(meta.domainKey));
    } else {
        QMessageBox::critical(this, "Failed", "Code generation failed. Check logs.");
    }
}

ClassMeta MainWindow::getCurrentClassMeta() const
{
    QModelIndex index = ui->treeView->currentIndex();
    if (!index.isValid()) {
        return {};
    }

    QStandardItem *classItem = resolveClassItem(m_model->itemFromIndex(index));
    ClassMeta meta;
    if (tryLoadManagedClassMetaFromFilesystem(classItem, &meta)) {
        return meta;
    }

    return classMetaFromItem(classItem);
}

void MainWindow::onTreeSelectionChanged() {
    // 在这里更新右侧属性面板
    QModelIndex index = ui->treeView->currentIndex();
    if (!index.isValid()) {
        clearClassInfoPanel();
        m_currentClassItem = nullptr;
        return;
    }
    
    QStandardItem *item = m_model->itemFromIndex(index);
    if (!item) {
        clearClassInfoPanel();
        m_currentClassItem = nullptr;
        return;
    }
    
    QString itemType = item->data(Qt::UserRole + 2).toString();
    
    // 如果选中的是Class节点，显示类信息
    if (itemType == "Class") {
        m_currentClassItem = item;
        updateClassInfoPanel(item);
    } else if (itemType == "Function") {
        // 如果选中的是Function节点，显示父类的信息
        QStandardItem *parentItem = item->parent();
        if (parentItem) {
            m_currentClassItem = parentItem;
            updateClassInfoPanel(parentItem);
        } else {
            clearClassInfoPanel();
            m_currentClassItem = nullptr;
        }
    } else if (itemType == "Domain") {
        // 如果选中的是Domain节点，清空面板
        clearClassInfoPanel();
        m_currentClassItem = nullptr;
    } else {
        clearClassInfoPanel();
        m_currentClassItem = nullptr;
    }
}

void MainWindow::clearClassInfoPanel()
{
    const QSignalBlocker classNameBlocker(ui->classNameEdit);
    const QSignalBlocker baseClassBlocker(ui->baseClassEdit);
    const QSignalBlocker functionsBlocker(ui->functionsTable);
    const QSignalBlocker membersBlocker(ui->membersTable);
    const QSignalBlocker parametersBlocker(ui->parametersTable);
    const QSignalBlocker constBlocker(ui->funcConstCheck);
    const QSignalBlocker staticBlocker(ui->funcStaticCheck);
    const QSignalBlocker virtualBlocker(ui->funcVirtualCheck);
    const QSignalBlocker accessBlocker(ui->funcAccessCombo);
    const QSignalBlocker memberAccessBlocker(ui->memberAccessCombo);
    const QSignalBlocker memberStaticBlocker(ui->memberStaticCheck);
    const QSignalBlocker memberConstexprBlocker(ui->memberConstexprCheck);

    ui->classNameEdit->clear();
    ui->baseClassEdit->setText("QObject");
    ui->functionsTable->setRowCount(0);
    ui->membersTable->setRowCount(0);
    ui->parametersTable->setRowCount(0);
    ui->funcConstCheck->setChecked(false);
    ui->funcStaticCheck->setChecked(false);
    ui->funcVirtualCheck->setChecked(false);
    ui->funcAccessCombo->setCurrentText("public");
    ui->memberAccessCombo->setCurrentText("private");
    ui->memberStaticCheck->setChecked(false);
    ui->memberConstexprCheck->setChecked(false);
    ui->headerPreviewEdit->clear();
    ui->sourcePreviewEdit->clear();
    m_currentClassItem = nullptr;
    updateValidationFeedback();
}

void MainWindow::updateClassInfoPanel(QStandardItem *classItem)
{
    if (!classItem) return;

    syncClassItemFromFilesystem(classItem);

    const QSignalBlocker classNameBlocker(ui->classNameEdit);
    const QSignalBlocker baseClassBlocker(ui->baseClassEdit);
    const QSignalBlocker functionsBlocker(ui->functionsTable);
    const QSignalBlocker membersBlocker(ui->membersTable);
    const QSignalBlocker parametersBlocker(ui->parametersTable);
    const QSignalBlocker constBlocker(ui->funcConstCheck);
    const QSignalBlocker staticBlocker(ui->funcStaticCheck);
    const QSignalBlocker virtualBlocker(ui->funcVirtualCheck);
    const QSignalBlocker accessBlocker(ui->funcAccessCombo);
    const QSignalBlocker memberAccessBlocker(ui->memberAccessCombo);
    const QSignalBlocker memberStaticBlocker(ui->memberStaticCheck);
    const QSignalBlocker memberConstexprBlocker(ui->memberConstexprCheck);

    const ClassMeta meta = classMetaFromItem(classItem);
    ui->classNameEdit->setText(meta.className);
    ui->baseClassEdit->setText(meta.baseClass);

    ui->functionsTable->setRowCount(meta.functions.count());
    for (int row = 0; row < meta.functions.count(); ++row) {
        const FunctionMeta &function = meta.functions.at(row);
        QTableWidgetItem *nameItem = createEditableItem(function.name);
        nameItem->setData(RoleFunctionParameters, QJsonDocument(parameterArrayFromMetaList(function.parameters)).toJson(QJsonDocument::Compact));
        ui->functionsTable->setItem(row, 0, nameItem);
        ui->functionsTable->setItem(row, 1, createEditableItem(function.returnType));
        ui->functionsTable->setItem(row, 2, createReadOnlyItem(parameterSummary(function.parameters)));
        ui->functionsTable->setItem(row, 3, createEditableItem(boolToYesNo(function.isConst)));
        ui->functionsTable->setItem(row, 4, createEditableItem(boolToYesNo(function.isStatic)));
        ui->functionsTable->setItem(row, 5, createEditableItem(boolToYesNo(function.isVirtual)));
        ui->functionsTable->setItem(row, 6, createEditableItem(normalizeAccessSpecifier(function.access, "public")));
    }

    ui->membersTable->setRowCount(meta.members.count());
    for (int row = 0; row < meta.members.count(); ++row) {
        const MemberMeta &member = meta.members.at(row);
        ui->membersTable->setItem(row, 0, createEditableItem(member.type));
        ui->membersTable->setItem(row, 1, createEditableItem(member.name));
        ui->membersTable->setItem(row, 2, createEditableItem(member.defaultValue));
        ui->membersTable->setItem(row, 3, createEditableItem(normalizeAccessSpecifier(member.access, "private")));
        ui->membersTable->setItem(row, 4, createEditableItem(boolToYesNo(member.isStatic)));
        ui->membersTable->setItem(row, 5, createEditableItem(boolToYesNo(member.isConstexpr)));
    }

    if (ui->functionsTable->rowCount() > 0) {
        ui->functionsTable->selectRow(0);
        loadParameterEditorFromSelectedFunction();
        loadFunctionOptionsFromSelectedFunction();
    } else {
        ui->parametersTable->setRowCount(0);
        ui->funcConstCheck->setChecked(false);
        ui->funcStaticCheck->setChecked(false);
        ui->funcVirtualCheck->setChecked(false);
        ui->funcAccessCombo->setCurrentText("public");
    }

    if (ui->membersTable->rowCount() > 0) {
        ui->membersTable->selectRow(0);
        loadMemberOptionsFromSelectedMember();
    } else {
        ui->memberAccessCombo->setCurrentText("private");
        ui->memberStaticCheck->setChecked(false);
        ui->memberConstexprCheck->setChecked(false);
    }

    refreshPreview();
    updateValidationFeedback();
}

ClassMeta MainWindow::getClassMetaFromPanel() const
{
    ClassMeta meta;
    meta.className = ui->classNameEdit->text();
    meta.baseClass = ui->baseClassEdit->text();
    if (meta.baseClass.isEmpty()) meta.baseClass = "QObject";
    
    if (!m_currentClassItem) {
        return meta;
    }
    
    meta.domainKey = m_currentClassItem->data(Qt::UserRole + 3).toString();
    
    // 从函数表格获取函数信息
    for (int i = 0; i < ui->functionsTable->rowCount(); ++i) {
        QTableWidgetItem *nameItem = ui->functionsTable->item(i, 0);
        QTableWidgetItem *returnItem = ui->functionsTable->item(i, 1);
        QTableWidgetItem *constItem = ui->functionsTable->item(i, 3);
        QTableWidgetItem *staticItem = ui->functionsTable->item(i, 4);
        QTableWidgetItem *virtualItem = ui->functionsTable->item(i, 5);
        QTableWidgetItem *accessItem = ui->functionsTable->item(i, 6);
        
        if (!nameItem || !returnItem) continue;
        
        FunctionMeta func;
        func.name = nameItem->text();
        func.returnType = returnItem->text();
        func.access = normalizeAccessSpecifier(accessItem ? accessItem->text() : QString(), "public");
        func.isConst = constItem && yesNoToBool(constItem->text());
        func.isStatic = staticItem && yesNoToBool(staticItem->text());
        func.isVirtual = virtualItem && yesNoToBool(virtualItem->text());
        func.parameters = functionParametersFromRow(i);
        
        meta.functions.append(func);
    }
    
    // 从成员变量表格获取成员变量信息
    for (int i = 0; i < ui->membersTable->rowCount(); ++i) {
        QTableWidgetItem *typeItem = ui->membersTable->item(i, 0);
        QTableWidgetItem *nameItem = ui->membersTable->item(i, 1);
        QTableWidgetItem *defaultItem = ui->membersTable->item(i, 2);
        
        if (!typeItem || !nameItem) continue;
        
        MemberMeta member;
        member.type = typeItem->text();
        member.name = nameItem->text();
        member.defaultValue = defaultItem ? defaultItem->text() : "";
        member.access = normalizeAccessSpecifier(ui->membersTable->item(i, 3) ? ui->membersTable->item(i, 3)->text() : QString(), "private");
        member.isStatic = ui->membersTable->item(i, 4) && yesNoToBool(ui->membersTable->item(i, 4)->text());
        member.isConstexpr = ui->membersTable->item(i, 5) && yesNoToBool(ui->membersTable->item(i, 5)->text());
        
        meta.members.append(member);
    }
    
    return meta;
}

void MainWindow::saveClassInfoToNode(QStandardItem *classItem, const ClassMeta &meta)
{
    if (!classItem) return;

    classItem->setData(meta.className, RoleName);
    classItem->setData(meta.baseClass, RoleBaseClass);

    QJsonArray funcArray;
    for (const auto &func : meta.functions) {
        funcArray.append(functionObjectFromMeta(func));
    }
    classItem->setData(QString::fromUtf8(QJsonDocument(funcArray).toJson(QJsonDocument::Compact)), RoleFunctions);

    QJsonArray memberArray;
    for (const auto &member : meta.members) {
        memberArray.append(memberObjectFromMeta(member));
    }
    classItem->setData(QString::fromUtf8(QJsonDocument(memberArray).toJson(QJsonDocument::Compact)), RoleMembers);

    classItem->removeRows(0, classItem->rowCount());
    for (const auto &func : meta.functions) {
        QStandardItem *funcItem = new QStandardItem(functionItemText(func));
        funcItem->setData("Function", RoleType);
        funcItem->setData(QString::fromUtf8(QJsonDocument(functionObjectFromMeta(func)).toJson(QJsonDocument::Compact)), RoleFunctions);
        classItem->appendRow(funcItem);
    }

    updateClassItemPresentation(classItem, meta);
}

void MainWindow::onAddFunctionClicked()
{
    if (!m_currentClassItem) {
        QMessageBox::warning(this, "Tip", "Please select a Class first.");
        return;
    }

    insertDefaultFunctionRow();
}

void MainWindow::onAddMemberClicked()
{
    if (!m_currentClassItem) {
        QMessageBox::warning(this, "Tip", "Please select a Class first.");
        return;
    }
    

    insertDefaultMemberRow();
}

void MainWindow::onDeleteItemClicked()
{
    const bool functionsTabActive = ui->editorTabs->currentIndex() == 0;

    if (functionsTabActive && ui->functionsTable->currentRow() >= 0) {
        ui->functionsTable->removeRow(ui->functionsTable->currentRow());
        loadParameterEditorFromSelectedFunction();
        refreshPreview();
        persistProjectModel();
        return;
    }

    if (!functionsTabActive && ui->membersTable->currentRow() >= 0) {
        ui->membersTable->removeRow(ui->membersTable->currentRow());
        loadMemberOptionsFromSelectedMember();
        refreshPreview();
        persistProjectModel();
        return;
    }

    QMessageBox::information(this, "Tip", functionsTabActive ? "Please select a function row to delete." : "Please select a member row to delete.");
}

void MainWindow::onSaveClassClicked()
{
    if (!m_currentClassItem) {
        QMessageBox::warning(this, "Tip", "Please select a Class first.");
        return;
    }
    
    // 从面板获取类信息
    ClassMeta meta = getClassMetaFromPanel();
    
    if (meta.className.isEmpty()) {
        QMessageBox::warning(this, "Error", "Class name cannot be empty.");
        return;
    }

    QString validationMessage;
    if (!validateClassMeta(meta, m_currentClassItem, &validationMessage)) {
        QMessageBox::warning(this, "Invalid Class Definition", validationMessage);
        return;
    }
    
    // 保存到树节点
    saveClassInfoToNode(m_currentClassItem, meta);
    
    // 生成代码
    bool success = m_engine->generateClass(meta, m_projectRootPath);
    if (success) {
        persistProjectModel();
        ui->logEdit->append(QString("Class '%1' saved and generated successfully.").arg(meta.className));
        QMessageBox::information(this, "Success",
                                 "Code generated successfully in:\n" + QDir(m_projectRootPath).filePath(meta.domainKey));
    } else {
        QMessageBox::critical(this, "Failed", "Code generation failed. Check logs.");
    }
}

void MainWindow::onActionGenerateAll()
{
    if (m_currentClassItem) {
        saveClassInfoToNode(m_currentClassItem, getClassMetaFromPanel());
    }

    // 遍历所有域和类，生成所有代码
    ui->logEdit->append("=== Starting Generate All ===");
    
    int generatedCount = 0;
    for (int r = 0; r < m_model->rowCount(); ++r) {
        QStandardItem *domainItem = m_model->item(r);
        if (!domainItem) continue;
        
        QString domainKey = domainItem->data(Qt::UserRole + 1).toString();
        ui->logEdit->append(QString("Processing domain: %1").arg(domainKey));
        
        // 遍历该域下的所有类
        for (int c = 0; c < domainItem->rowCount(); ++c) {
            QStandardItem *classItem = domainItem->child(c);
            if (!classItem) continue;
            if (classItem->data(Qt::UserRole + 2).toString() != "Class") continue;

            ClassMeta meta = classMetaFromItem(classItem);
            meta.domainKey = domainKey;

            QString validationMessage;
            if (!validateClassMeta(meta, classItem, &validationMessage)) {
                QMessageBox::warning(this, "Invalid Class Definition",
                                     QString("Class '%1' is invalid:\n\n%2").arg(meta.className, validationMessage));
                return;
            }
            
            bool success = m_engine->generateClass(meta, m_projectRootPath);
            if (success) {
                generatedCount++;
                ui->logEdit->append(QString("  Generated: %1").arg(meta.className));
            } else {
                ui->logEdit->append(QString("  Failed: %1").arg(meta.className));
            }
        }
    }
    
    ui->logEdit->append(QString("=== Generate All Complete: %1 classes ===").arg(generatedCount));
    persistProjectModel();
    QMessageBox::information(this, "Complete", QString("Generated %1 classes.").arg(generatedCount));
}

void MainWindow::onActionOpenProject()
{
    persistProjectModel();

    QString dir = QFileDialog::getExistingDirectory(this, "Select Project Directory", 
                                                     m_projectRootPath);
    if (!dir.isEmpty()) {
        m_projectRootPath = dir;
        updateOutputPathDisplay();
        ui->logEdit->append(QString("Project root set to: %1").arg(dir));

        if (!loadProjectModel(m_projectRootPath)) {
            initializeDefaultDomains();
            clearClassInfoPanel();
            const bool imported = reconcileModelWithFilesystem();
            refreshTreePresentation();
            ui->treeView->expandAll();
            if (imported) {
                persistProjectModel();
                ui->logEdit->append("No existing project metadata found. Imported managed classes from disk.");
            } else {
                ui->logEdit->append("No existing project metadata found. Started a new model.");
            }
        }
    }
}

void MainWindow::refreshPreview()
{
    if (!m_currentClassItem) {
        ui->headerPreviewEdit->clear();
        ui->sourcePreviewEdit->clear();
        return;
    }

    const ClassMeta meta = getClassMetaFromPanel();
    if (meta.className.trimmed().isEmpty()) {
        ui->headerPreviewEdit->setPlainText("Select or edit a class to preview generated code.");
        ui->sourcePreviewEdit->clear();
        return;
    }

    ui->headerPreviewEdit->setPlainText(m_engine->previewHeader(meta));
    ui->sourcePreviewEdit->setPlainText(m_engine->previewSource(meta));
}

void MainWindow::onFunctionSelectionChanged()
{
    loadParameterEditorFromSelectedFunction();
    loadFunctionOptionsFromSelectedFunction();
    updateValidationFeedback();
}

void MainWindow::onFunctionFlagsChanged()
{
    syncSelectedFunctionFlags();
}

void MainWindow::onMemberSelectionChanged()
{
    loadMemberOptionsFromSelectedMember();
    updateValidationFeedback();
}

void MainWindow::onMemberFlagsChanged()
{
    syncSelectedMemberFlags();
}

void MainWindow::onEditorTabChanged(int)
{
    updateEditorActionButtons();
}

void MainWindow::onAddParameterClicked()
{
    if (ui->functionsTable->currentRow() < 0) {
        QMessageBox::information(this, "Tip", "Please select a function first.");
        return;
    }

    const QSignalBlocker blocker(ui->parametersTable);
    const int row = ui->parametersTable->rowCount();
    ui->parametersTable->insertRow(row);
    ui->parametersTable->setItem(row, 0, createEditableItem("int"));
    ui->parametersTable->setItem(row, 1, createEditableItem(QString("arg%1").arg(row + 1)));
    ui->parametersTable->setItem(row, 2, createReadOnlyItem(defaultValueForType("int")));
    ui->parametersTable->setItem(row, 3, createEditableItem("No"));
    ui->parametersTable->setCurrentCell(row, 0);
    syncSelectedFunctionParameters();
    updateValidationFeedback();
}

void MainWindow::onDeleteParameterClicked()
{
    if (ui->parametersTable->currentRow() < 0) {
        QMessageBox::information(this, "Tip", "Please select a parameter first.");
        return;
    }

    ui->parametersTable->removeRow(ui->parametersTable->currentRow());
    syncSelectedFunctionParameters();
    updateValidationFeedback();
}

void MainWindow::loadParameterEditorFromSelectedFunction()
{
    const QSignalBlocker blocker(ui->parametersTable);
    ui->parametersTable->setRowCount(0);

    const int row = ui->functionsTable->currentRow();
    if (row < 0) {
        return;
    }

    const QList<ParamMeta> parameters = functionParametersFromRow(row);
    ui->parametersTable->setRowCount(parameters.count());
    for (int index = 0; index < parameters.count(); ++index) {
        const ParamMeta &param = parameters.at(index);
        ui->parametersTable->setItem(index, 0, createEditableItem(param.type));
        ui->parametersTable->setItem(index, 1, createEditableItem(param.name));
        ui->parametersTable->setItem(index, 2, createReadOnlyItem(param.defaultValue.isEmpty() ? defaultValueForType(param.type) : param.defaultValue));
        ui->parametersTable->setItem(index, 3, createEditableItem(param.isConstRef ? "Yes" : "No"));
    }
}

void MainWindow::loadFunctionOptionsFromSelectedFunction()
{
    const QSignalBlocker constBlocker(ui->funcConstCheck);
    const QSignalBlocker staticBlocker(ui->funcStaticCheck);
    const QSignalBlocker virtualBlocker(ui->funcVirtualCheck);
    const QSignalBlocker accessBlocker(ui->funcAccessCombo);

    const int row = ui->functionsTable->currentRow();
    if (row < 0) {
        ui->funcConstCheck->setChecked(false);
        ui->funcStaticCheck->setChecked(false);
        ui->funcVirtualCheck->setChecked(false);
        ui->funcAccessCombo->setCurrentText("public");
        return;
    }

    ui->funcConstCheck->setChecked(ui->functionsTable->item(row, 3) && yesNoToBool(ui->functionsTable->item(row, 3)->text()));
    ui->funcStaticCheck->setChecked(ui->functionsTable->item(row, 4) && yesNoToBool(ui->functionsTable->item(row, 4)->text()));
    ui->funcVirtualCheck->setChecked(ui->functionsTable->item(row, 5) && yesNoToBool(ui->functionsTable->item(row, 5)->text()));
    ui->funcAccessCombo->setCurrentText(normalizeAccessSpecifier(ui->functionsTable->item(row, 6) ? ui->functionsTable->item(row, 6)->text() : QString(), "public"));
}

void MainWindow::syncSelectedFunctionParameters()
{
    const int row = ui->functionsTable->currentRow();
    if (row < 0) {
        return;
    }

    QList<ParamMeta> parameters;
    for (int index = 0; index < ui->parametersTable->rowCount(); ++index) {
        QTableWidgetItem *typeItem = ui->parametersTable->item(index, 0);
        QTableWidgetItem *nameItem = ui->parametersTable->item(index, 1);
        if (!typeItem || !nameItem || typeItem->text().trimmed().isEmpty() || nameItem->text().trimmed().isEmpty()) {
            continue;
        }

        ParamMeta param;
        param.type = typeItem->text().trimmed();
        param.name = nameItem->text().trimmed();
        param.defaultValue = defaultValueForType(param.type);
        param.isConstRef = ui->parametersTable->item(index, 3) && ui->parametersTable->item(index, 3)->text().compare("Yes", Qt::CaseInsensitive) == 0;
        parameters.append(param);

        if (QTableWidgetItem *defaultItem = ui->parametersTable->item(index, 2)) {
            defaultItem->setText(param.defaultValue);
        } else {
            ui->parametersTable->setItem(index, 2, createReadOnlyItem(param.defaultValue));
        }
    }

    if (QTableWidgetItem *nameItem = ui->functionsTable->item(row, 0)) {
        nameItem->setData(RoleFunctionParameters, QJsonDocument(parameterArrayFromMetaList(parameters)).toJson(QJsonDocument::Compact));
    }

    const QSignalBlocker blocker(ui->functionsTable);
    if (QTableWidgetItem *paramsItem = ui->functionsTable->item(row, 2)) {
        paramsItem->setText(parameterSummary(parameters));
    } else {
        ui->functionsTable->setItem(row, 2, createReadOnlyItem(parameterSummary(parameters)));
    }

    refreshPreview();
    persistProjectModel();
}

void MainWindow::syncSelectedFunctionFlags()
{
    const int row = ui->functionsTable->currentRow();
    if (row < 0) {
        return;
    }

    const QSignalBlocker blocker(ui->functionsTable);
    if (QTableWidgetItem *constItem = ui->functionsTable->item(row, 3)) {
        constItem->setText(boolToYesNo(ui->funcConstCheck->isChecked()));
    } else {
        ui->functionsTable->setItem(row, 3, new QTableWidgetItem(boolToYesNo(ui->funcConstCheck->isChecked())));
    }

    if (QTableWidgetItem *staticItem = ui->functionsTable->item(row, 4)) {
        staticItem->setText(boolToYesNo(ui->funcStaticCheck->isChecked()));
    } else {
        ui->functionsTable->setItem(row, 4, new QTableWidgetItem(boolToYesNo(ui->funcStaticCheck->isChecked())));
    }

    if (QTableWidgetItem *virtualItem = ui->functionsTable->item(row, 5)) {
        virtualItem->setText(boolToYesNo(ui->funcVirtualCheck->isChecked()));
    } else {
        ui->functionsTable->setItem(row, 5, new QTableWidgetItem(boolToYesNo(ui->funcVirtualCheck->isChecked())));
    }

    if (QTableWidgetItem *accessItem = ui->functionsTable->item(row, 6)) {
        accessItem->setText(normalizeAccessSpecifier(ui->funcAccessCombo->currentText(), "public"));
    } else {
        ui->functionsTable->setItem(row, 6, new QTableWidgetItem(normalizeAccessSpecifier(ui->funcAccessCombo->currentText(), "public")));
    }

    refreshPreview();
    persistProjectModel();
}

void MainWindow::loadMemberOptionsFromSelectedMember()
{
    const QSignalBlocker accessBlocker(ui->memberAccessCombo);
    const QSignalBlocker staticBlocker(ui->memberStaticCheck);
    const QSignalBlocker constexprBlocker(ui->memberConstexprCheck);

    const int row = ui->membersTable->currentRow();
    if (row < 0) {
        ui->memberAccessCombo->setCurrentText("private");
        ui->memberStaticCheck->setChecked(false);
        ui->memberConstexprCheck->setChecked(false);
        return;
    }

    ui->memberAccessCombo->setCurrentText(normalizeAccessSpecifier(ui->membersTable->item(row, 3) ? ui->membersTable->item(row, 3)->text() : QString(), "private"));
    ui->memberStaticCheck->setChecked(ui->membersTable->item(row, 4) && yesNoToBool(ui->membersTable->item(row, 4)->text()));
    ui->memberConstexprCheck->setChecked(ui->membersTable->item(row, 5) && yesNoToBool(ui->membersTable->item(row, 5)->text()));
}

void MainWindow::syncSelectedMemberFlags()
{
    const int row = ui->membersTable->currentRow();
    if (row < 0) {
        return;
    }

    const QSignalBlocker blocker(ui->membersTable);
    if (QTableWidgetItem *accessItem = ui->membersTable->item(row, 3)) {
        accessItem->setText(normalizeAccessSpecifier(ui->memberAccessCombo->currentText(), "private"));
    } else {
        ui->membersTable->setItem(row, 3, createEditableItem(normalizeAccessSpecifier(ui->memberAccessCombo->currentText(), "private")));
    }

    if (QTableWidgetItem *staticItem = ui->membersTable->item(row, 4)) {
        staticItem->setText(boolToYesNo(ui->memberStaticCheck->isChecked()));
    } else {
        ui->membersTable->setItem(row, 4, createEditableItem(boolToYesNo(ui->memberStaticCheck->isChecked())));
    }

    if (QTableWidgetItem *constexprItem = ui->membersTable->item(row, 5)) {
        constexprItem->setText(boolToYesNo(ui->memberConstexprCheck->isChecked()));
    } else {
        ui->membersTable->setItem(row, 5, createEditableItem(boolToYesNo(ui->memberConstexprCheck->isChecked())));
    }

    refreshPreview();
    persistProjectModel();
}

QList<ParamMeta> MainWindow::functionParametersFromRow(int row) const
{
    if (row < 0) {
        return {};
    }

    if (QTableWidgetItem *nameItem = ui->functionsTable->item(row, 0)) {
        const QString json = nameItem->data(RoleFunctionParameters).toString();
        if (!json.isEmpty()) {
            return parameterMetaListFromArray(jsonArrayFromString(json));
        }
    }

    if (QTableWidgetItem *paramsItem = ui->functionsTable->item(row, 2)) {
        return parameterMetaListFromString(paramsItem->text());
    }

    return {};
}

QString MainWindow::projectMetadataFilePath() const
{
    return QDir(m_projectRootPath).filePath(kProjectMetadataFileName);
}

bool MainWindow::persistProjectModel()
{
    if (m_projectRootPath.isEmpty()) {
        return false;
    }

    QDir rootDir(m_projectRootPath);
    if (!rootDir.exists() && !rootDir.mkpath(".")) {
        ui->logEdit->append(QString("Failed to create output directory: %1").arg(m_projectRootPath));
        return false;
    }

    if (m_currentClassItem) {
        saveClassInfoToNode(m_currentClassItem, getClassMetaFromPanel());
    }

    QJsonObject rootObject;
    rootObject["version"] = 1;
    rootObject["outputRoot"] = QDir::toNativeSeparators(m_projectRootPath);

    QJsonArray domains;
    for (int row = 0; row < m_model->rowCount(); ++row) {
        QStandardItem *domainItem = m_model->item(row);
        if (!domainItem || domainItem->data(RoleType).toString() != "Domain") {
            continue;
        }

        QJsonObject domainObject;
        domainObject["key"] = domainItem->data(RoleName).toString();

        QJsonArray classes;
        for (int childRow = 0; childRow < domainItem->rowCount(); ++childRow) {
            QStandardItem *classItem = domainItem->child(childRow);
            if (!classItem || classItem->data(RoleType).toString() != "Class") {
                continue;
            }

            classes.append(classMetaToJsonObject(classMetaFromItem(classItem)));
        }

        domainObject["classes"] = classes;
        domains.append(domainObject);
    }

    rootObject["domains"] = domains;

    QFile file(projectMetadataFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        ui->logEdit->append(QString("Failed to save project metadata: %1").arg(file.errorString()));
        return false;
    }

    file.write(QJsonDocument(rootObject).toJson(QJsonDocument::Indented));
    file.close();
    statusBar()->showMessage(QString("Project model saved: %1").arg(QDir::toNativeSeparators(projectMetadataFilePath())), 3000);
    return true;
}

bool MainWindow::loadProjectModel(const QString &rootPath)
{
    const QString metadataPath = QDir(rootPath).filePath(kProjectMetadataFileName);
    QFile file(metadataPath);
    if (!file.exists()) {
        return false;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ui->logEdit->append(QString("Failed to open project metadata: %1").arg(file.errorString()));
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!document.isObject()) {
        ui->logEdit->append("Project metadata format is invalid.");
        return false;
    }

    initializeDefaultDomains();
    clearClassInfoPanel();

    const QJsonArray domains = document.object().value("domains").toArray();
    for (const auto &domainValue : domains) {
        if (!domainValue.isObject()) {
            continue;
        }

        const QJsonObject domainObject = domainValue.toObject();
        const QString domainKey = domainObject.value("key").toString();
        if (domainKey.isEmpty()) {
            continue;
        }

        QStandardItem *domainItem = findDomainItem(m_model, domainKey);
        if (!domainItem) {
            domainItem = new QStandardItem(domainKey);
            domainItem->setData(domainKey, RoleName);
            domainItem->setData("Domain", RoleType);
            m_model->appendRow(domainItem);
        }

        domainItem->removeRows(0, domainItem->rowCount());
        for (const auto &classValue : domainObject.value("classes").toArray()) {
            if (!classValue.isObject()) {
                continue;
            }

            ClassMeta meta = classMetaFromJsonObject(classValue.toObject());
            if (meta.className.isEmpty()) {
                continue;
            }

            meta.domainKey = domainKey;
            QStandardItem *classItem = new QStandardItem(meta.className);
            classItem->setData(meta.className, RoleName);
            classItem->setData("Class", RoleType);
            classItem->setData(domainKey, RoleDomain);
            classItem->setData(meta.baseClass, RoleBaseClass);
            saveClassInfoToNode(classItem, meta);
            domainItem->appendRow(classItem);
        }
    }

    const bool reconciled = reconcileModelWithFilesystem();
    refreshTreePresentation();
    ui->treeView->expandAll();
    ui->logEdit->append(QString("Loaded project metadata from: %1").arg(QDir::toNativeSeparators(metadataPath)));
    if (reconciled) {
        persistProjectModel();
    }
    statusBar()->showMessage("Project model loaded", 3000);
    return true;
}

bool MainWindow::reconcileModelWithFilesystem()
{
    bool changed = false;
    bool currentClassRemoved = false;

    for (int row = 0; row < m_model->rowCount(); ++row) {
        QStandardItem *domainItem = m_model->item(row);
        if (!domainItem || domainItem->data(RoleType).toString() != "Domain") {
            continue;
        }

        const QString domainKey = domainItem->data(RoleName).toString();
        QDir domainDir(QDir(m_projectRootPath).filePath(domainKey));

        QSet<QString> knownClasses;
        for (int childRow = 0; childRow < domainItem->rowCount(); ++childRow) {
            QStandardItem *classItem = domainItem->child(childRow);
            if (classItem && classItem->data(RoleType).toString() == "Class") {
                knownClasses.insert(classItem->data(RoleName).toString());
            }
        }

        for (int childRow = domainItem->rowCount() - 1; childRow >= 0; --childRow) {
            QStandardItem *classItem = domainItem->child(childRow);
            if (!classItem || classItem->data(RoleType).toString() != "Class") {
                continue;
            }

            const QString className = classItem->data(RoleName).toString();
            const bool hasHeader = domainDir.exists(className + ".h") || domainDir.exists(className + ".hpp");
            const bool hasSource = domainDir.exists(className + ".cpp") || domainDir.exists(className + ".cc") || domainDir.exists(className + ".cxx");
            if (hasHeader || hasSource) {
                changed = syncClassItemFromFilesystem(classItem) || changed;
                continue;
            }

            if (classItem == m_currentClassItem) {
                currentClassRemoved = true;
            }

            domainItem->removeRow(childRow);
            ui->logEdit->append(QString("Removed missing class from model: %1/%2").arg(domainKey, className));
            changed = true;
            knownClasses.remove(className);
        }

        const QFileInfoList headerFiles = domainDir.entryInfoList({"*.h", "*.hpp"}, QDir::Files, QDir::Name);
        for (const QFileInfo &headerFile : headerFiles) {
            const QString className = headerFile.completeBaseName();
            if (knownClasses.contains(className)) {
                continue;
            }

            QString sourcePath;
            for (const QString &extension : {QString("cpp"), QString("cc"), QString("cxx")}) {
                const QString candidate = domainDir.filePath(className + "." + extension);
                if (QFile::exists(candidate)) {
                    sourcePath = candidate;
                    break;
                }
            }
            if (sourcePath.isEmpty()) {
                continue;
            }

            QFile headerSource(headerFile.absoluteFilePath());
            QFile sourceFile(sourcePath);
            if (!headerSource.open(QIODevice::ReadOnly | QIODevice::Text) || !sourceFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                continue;
            }

            const QString headerContent = QString::fromUtf8(headerSource.readAll());
            const QString sourceContent = QString::fromUtf8(sourceFile.readAll());
            if (!isManagedGeneratedClassFileSet(headerContent, sourceContent, domainKey)) {
                continue;
            }

            ClassMeta meta = parseManagedClassMetaFromFiles(headerContent, domainKey);
            if (meta.className.isEmpty() || meta.className != className) {
                continue;
            }

            QStandardItem *classItem = new QStandardItem(meta.className);
            classItem->setData(meta.className, RoleName);
            classItem->setData("Class", RoleType);
            classItem->setData(domainKey, RoleDomain);
            classItem->setData(meta.baseClass, RoleBaseClass);
            saveClassInfoToNode(classItem, meta);
            domainItem->appendRow(classItem);
            knownClasses.insert(meta.className);
            ui->logEdit->append(QString("Imported managed class from disk: %1/%2").arg(domainKey, meta.className));
            changed = true;
        }
    }

    if (currentClassRemoved) {
        clearClassInfoPanel();
        m_currentClassItem = nullptr;
    }

    return changed;
}

bool MainWindow::tryLoadManagedClassMetaFromFilesystem(QStandardItem *classItem, ClassMeta *meta) const
{
    if (!classItem || !meta || classItem->data(RoleType).toString() != "Class") {
        return false;
    }

    const QString domainKey = classItem->data(RoleDomain).toString();
    const QString className = classItem->data(RoleName).toString();
    if (domainKey.isEmpty() || className.isEmpty() || m_projectRootPath.isEmpty()) {
        return false;
    }

    const QDir domainDir(QDir(m_projectRootPath).filePath(domainKey));
    QString headerPath;
    for (const QString &extension : {QString("h"), QString("hpp")}) {
        const QString candidate = domainDir.filePath(className + "." + extension);
        if (QFile::exists(candidate)) {
            headerPath = candidate;
            break;
        }
    }

    QString sourcePath;
    for (const QString &extension : {QString("cpp"), QString("cc"), QString("cxx")}) {
        const QString candidate = domainDir.filePath(className + "." + extension);
        if (QFile::exists(candidate)) {
            sourcePath = candidate;
            break;
        }
    }

    if (headerPath.isEmpty() || sourcePath.isEmpty()) {
        return false;
    }

    QFile headerFile(headerPath);
    QFile sourceFile(sourcePath);
    if (!headerFile.open(QIODevice::ReadOnly | QIODevice::Text) || !sourceFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    const QString headerContent = QString::fromUtf8(headerFile.readAll());
    const QString sourceContent = QString::fromUtf8(sourceFile.readAll());
    if (!isManagedGeneratedClassFileSet(headerContent, sourceContent, domainKey)) {
        return false;
    }

    ClassMeta parsed = parseManagedClassMetaFromFiles(headerContent, domainKey);
    if (parsed.className.isEmpty() || parsed.className != className) {
        return false;
    }

    *meta = parsed;
    return true;
}

bool MainWindow::syncClassItemFromFilesystem(QStandardItem *classItem)
{
    if (!classItem || classItem->data(RoleType).toString() != "Class") {
        return false;
    }

    ClassMeta diskMeta;
    if (!tryLoadManagedClassMetaFromFilesystem(classItem, &diskMeta)) {
        return false;
    }

    const ClassMeta cachedMeta = classMetaFromItem(classItem);
    if (QJsonDocument(classMetaToJsonObject(cachedMeta)).toJson(QJsonDocument::Compact)
        == QJsonDocument(classMetaToJsonObject(diskMeta)).toJson(QJsonDocument::Compact)) {
        return false;
    }

    saveClassInfoToNode(classItem, diskMeta);
    return true;
}

void MainWindow::onActionAbout()
{
    QMessageBox::about(this, "About ArchitectGen",
                       "<h3>ArchitectGen v1.0</h3>"
                       "<p>C++ Architecture Generator</p>"
                       "<p>Built with Qt6 & C++17</p>"
                       "<p>Generate domain-driven C++ project structure with smart code templates.</p>");
}
