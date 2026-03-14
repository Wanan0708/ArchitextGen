#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "GeneratorEngine.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QAction>
#include <QCoreApplication>
#include <QDebug>
#include <QSet>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTableWidgetItem>

namespace {
const char kProjectMetadataFileName[] = "architectgen.project.json";

constexpr int RoleName = Qt::UserRole + 1;
constexpr int RoleType = Qt::UserRole + 2;
constexpr int RoleDomain = Qt::UserRole + 3;
constexpr int RoleFunctions = Qt::UserRole + 4;
constexpr int RoleBaseClass = Qt::UserRole + 5;
constexpr int RoleMembers = Qt::UserRole + 6;
constexpr int RoleFunctionParameters = Qt::UserRole + 20;

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
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_engine(new GeneratorEngine(this))
    , m_currentClassItem(nullptr)
{
    ui->setupUi(this);

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
    connect(ui->browseProjectBtn, &QPushButton::clicked, this, &MainWindow::onActionOpenProject);
    connect(ui->generateCurrentBtn, &QPushButton::clicked, this, &MainWindow::onActionGenerate);
    connect(ui->generateAllBtn, &QPushButton::clicked, this, &MainWindow::onActionGenerateAll);
    
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
    connect(ui->classNameEdit, &QLineEdit::textChanged, this, [this] { refreshPreview(); });
    connect(ui->baseClassEdit, &QLineEdit::textChanged, this, [this] { refreshPreview(); });
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
        persistProjectModel();
    });
    connect(ui->membersTable, &QTableWidget::itemChanged, this, [this] {
        refreshPreview();
        persistProjectModel();
    });
    connect(ui->parametersTable, &QTableWidget::itemChanged, this, [this] { syncSelectedFunctionParameters(); });

    m_templateRootPath = QDir(QCoreApplication::applicationDirPath() + "/../../../templates").absolutePath();
    m_projectRootPath = QDir(QCoreApplication::applicationDirPath() + "/../../../generated").absolutePath();

    QDir().mkpath(m_projectRootPath);
    updateOutputPathDisplay();

    ui->logEdit->append(QString("Template root: %1").arg(QDir::toNativeSeparators(m_templateRootPath)));
    ui->logEdit->append(QString("Output path: %1").arg(QDir::toNativeSeparators(m_projectRootPath)));

    initializeDefaultDomains();
    loadProjectModel(m_projectRootPath);

    // 连接引擎日志到 UI
    connect(m_engine, &GeneratorEngine::logMessage, this, [this](const QString &msg){
        ui->logEdit->append(msg);
        qDebug() << "[GenCore]" << msg;
    });
    
    // 初始化函数表格
    ui->functionsTable->setColumnWidth(0, 150);
    ui->functionsTable->setColumnWidth(1, 100);
    ui->functionsTable->setColumnWidth(2, 200);
    ui->functionsTable->setColumnWidth(3, 60);
    ui->functionsTable->setColumnWidth(4, 60);
    ui->functionsTable->setColumnWidth(5, 60);
    ui->functionsTable->setColumnWidth(6, 90);
    
    // 初始化成员变量表格
    ui->membersTable->setColumnWidth(0, 150);
    ui->membersTable->setColumnWidth(1, 150);
    ui->membersTable->setColumnWidth(2, 100);
    ui->membersTable->setColumnWidth(3, 90);

    ui->parametersTable->setColumnWidth(0, 150);
    ui->parametersTable->setColumnWidth(1, 140);
    ui->parametersTable->setColumnWidth(2, 140);
    ui->parametersTable->setColumnWidth(3, 80);

    refreshPreview();
}

MainWindow::~MainWindow()
{
    delete ui;
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
}

void MainWindow::updateOutputPathDisplay()
{
    ui->outputPathEdit->setText(QDir::toNativeSeparators(m_projectRootPath));
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
    
    bool ok;
    QString className = QInputDialog::getText(this, "New Class", "Enter Class Name:", QLineEdit::Normal, "", &ok);
    if (ok && !className.isEmpty()) {
        QStandardItem *classItem = new QStandardItem(className);
        classItem->setData(className, Qt::UserRole + 1); // ClassName
        classItem->setData("Class", Qt::UserRole + 2);   // Type
        classItem->setData(domainItem->data(Qt::UserRole + 1).toString(), Qt::UserRole + 3); // Parent Domain Key
        classItem->setData("QObject", Qt::UserRole + 5); // BaseClass
        
        // 初始化空的函数和成员变量JSON
        QJsonArray emptyArray;
        classItem->setData(QString::fromUtf8(QJsonDocument(emptyArray).toJson(QJsonDocument::Compact)), Qt::UserRole + 4); // Functions JSON
        classItem->setData(QString::fromUtf8(QJsonDocument(emptyArray).toJson(QJsonDocument::Compact)), Qt::UserRole + 6); // Members JSON
        
        domainItem->appendRow(classItem);
        ui->treeView->expand(domainItem->index());
        
        // 选中新添加的节点
        ui->treeView->setCurrentIndex(classItem->index());
        persistProjectModel();
    }
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

    bool ok;
    QString className = QInputDialog::getText(this, "New Class", "Enter Class Name:", QLineEdit::Normal, "", &ok);
    if (ok && !className.isEmpty()) {
        QStandardItem *classItem = new QStandardItem(className);
        classItem->setData(className, Qt::UserRole + 1); // ClassName
        classItem->setData("Class", Qt::UserRole + 2);   // Type
        classItem->setData(parentItem->data(Qt::UserRole + 1).toString(), Qt::UserRole + 3); // Parent Domain Key
        classItem->setData("QObject", Qt::UserRole + 5); // BaseClass
        
        // 初始化空的函数和成员变量JSON
        QJsonArray emptyArray;
        classItem->setData(QString::fromUtf8(QJsonDocument(emptyArray).toJson(QJsonDocument::Compact)), Qt::UserRole + 4); // Functions JSON
        classItem->setData(QString::fromUtf8(QJsonDocument(emptyArray).toJson(QJsonDocument::Compact)), Qt::UserRole + 6); // Members JSON

        parentItem->appendRow(classItem);
        ui->treeView->expand(parentItem->index());
        
        // 选中新添加的节点
        ui->treeView->setCurrentIndex(classItem->index());
        persistProjectModel();
    }
}

void MainWindow::onActionNewFunction()
{
    QModelIndex index = ui->treeView->currentIndex();
    if (!index.isValid()) {
        QMessageBox::warning(this, "Tip", "Please select a Class to add function.");
        return;
    }

    QStandardItem *item = m_model->itemFromIndex(index);
    if (item->data(Qt::UserRole + 2).toString() != "Class") {
        QMessageBox::warning(this, "Tip", "Please select a Class node.");
        return;
    }

    bool ok;
    QString funcName = QInputDialog::getText(this, "New Function", "Function name:", QLineEdit::Normal, "", &ok);
    if (!ok || funcName.isEmpty()) return;

    QString returnType = QInputDialog::getText(this, "New Function", "Return type:", QLineEdit::Normal, "void", &ok);
    if (!ok || returnType.isEmpty()) return;

    int paramCount = QInputDialog::getInt(this, "New Function", "Parameter count:", 0, 0, 32, 1, &ok);
    if (!ok) return;

    QJsonArray paramsArr;
    for (int i = 0; i < paramCount; ++i) {
        QString pType = QInputDialog::getText(this, "Param", QString("Param %1 type:").arg(i+1), QLineEdit::Normal, "int", &ok);
        if (!ok) return;
        QString pName = QInputDialog::getText(this, "Param", QString("Param %1 name:").arg(i+1), QLineEdit::Normal, QString("p%1").arg(i+1), &ok);
        if (!ok) return;
        QJsonObject pObj;
        pObj["type"] = pType;
        pObj["name"] = pName;
        pObj["defaultValue"] = "";
        pObj["isConstRef"] = false;
        paramsArr.append(pObj);
    }

    // 简单选择是否 const
    bool isConst = false;
    int rc = QMessageBox::question(this, "Const Method?", "Should the method be const?", QMessageBox::Yes | QMessageBox::No);
    if (rc == QMessageBox::Yes) isConst = true;

    QJsonObject funcObj;
    funcObj["name"] = funcName;
    funcObj["returnType"] = returnType;
    funcObj["access"] = "public";
    funcObj["isConst"] = isConst;
    funcObj["isStatic"] = false;
    funcObj["isVirtual"] = false;
    funcObj["parameters"] = paramsArr;

    QStandardItem *funcItem = new QStandardItem(funcName + "() : " + returnType);
    funcItem->setData("Function", Qt::UserRole + 2);
    funcItem->setData(QString::fromUtf8(QJsonDocument(funcObj).toJson(QJsonDocument::Compact)), Qt::UserRole + 4);

    item->appendRow(funcItem);
    ui->treeView->expand(item->index());
    
    // 更新右侧面板
    updateClassInfoPanel(item);
    persistProjectModel();
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

    return classMetaFromItem(m_model->itemFromIndex(index));
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

    ui->classNameEdit->clear();
    ui->baseClassEdit->setText("QObject");
    ui->functionsTable->setRowCount(0);
    ui->membersTable->setRowCount(0);
    ui->parametersTable->setRowCount(0);
    ui->funcConstCheck->setChecked(false);
    ui->funcStaticCheck->setChecked(false);
    ui->funcVirtualCheck->setChecked(false);
    ui->funcAccessCombo->setCurrentText("public");
    ui->headerPreviewEdit->clear();
    ui->sourcePreviewEdit->clear();
    m_currentClassItem = nullptr;
}

void MainWindow::updateClassInfoPanel(QStandardItem *classItem)
{
    if (!classItem) return;

    const QSignalBlocker classNameBlocker(ui->classNameEdit);
    const QSignalBlocker baseClassBlocker(ui->baseClassEdit);
    const QSignalBlocker functionsBlocker(ui->functionsTable);
    const QSignalBlocker membersBlocker(ui->membersTable);
    const QSignalBlocker parametersBlocker(ui->parametersTable);
    const QSignalBlocker constBlocker(ui->funcConstCheck);
    const QSignalBlocker staticBlocker(ui->funcStaticCheck);
    const QSignalBlocker virtualBlocker(ui->funcVirtualCheck);
    const QSignalBlocker accessBlocker(ui->funcAccessCombo);

    const ClassMeta meta = classMetaFromItem(classItem);
    ui->classNameEdit->setText(meta.className);
    ui->baseClassEdit->setText(meta.baseClass);

    ui->functionsTable->setRowCount(meta.functions.count());
    for (int row = 0; row < meta.functions.count(); ++row) {
        const FunctionMeta &function = meta.functions.at(row);
        QTableWidgetItem *nameItem = new QTableWidgetItem(function.name);
        nameItem->setData(RoleFunctionParameters, QJsonDocument(parameterArrayFromMetaList(function.parameters)).toJson(QJsonDocument::Compact));
        ui->functionsTable->setItem(row, 0, nameItem);
        ui->functionsTable->setItem(row, 1, new QTableWidgetItem(function.returnType));
        ui->functionsTable->setItem(row, 2, new QTableWidgetItem(parameterSummary(function.parameters)));
        ui->functionsTable->setItem(row, 3, new QTableWidgetItem(boolToYesNo(function.isConst)));
        ui->functionsTable->setItem(row, 4, new QTableWidgetItem(boolToYesNo(function.isStatic)));
        ui->functionsTable->setItem(row, 5, new QTableWidgetItem(boolToYesNo(function.isVirtual)));
        ui->functionsTable->setItem(row, 6, new QTableWidgetItem(normalizeAccessSpecifier(function.access, "public")));
    }

    ui->membersTable->setRowCount(meta.members.count());
    for (int row = 0; row < meta.members.count(); ++row) {
        const MemberMeta &member = meta.members.at(row);
        ui->membersTable->setItem(row, 0, new QTableWidgetItem(member.type));
        ui->membersTable->setItem(row, 1, new QTableWidgetItem(member.name));
        ui->membersTable->setItem(row, 2, new QTableWidgetItem(member.defaultValue));
        ui->membersTable->setItem(row, 3, new QTableWidgetItem(normalizeAccessSpecifier(member.access, "private")));
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

    refreshPreview();
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
        
        meta.members.append(member);
    }
    
    return meta;
}

void MainWindow::saveClassInfoToNode(QStandardItem *classItem, const ClassMeta &meta)
{
    if (!classItem) return;

    classItem->setData(meta.className, RoleName);
    classItem->setText(meta.className);
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
}

void MainWindow::onAddFunctionClicked()
{
    if (!m_currentClassItem) {
        QMessageBox::warning(this, "Tip", "Please select a Class first.");
        return;
    }
    
    bool ok;
    QString funcName = QInputDialog::getText(this, "Add Function", "Function name:", QLineEdit::Normal, "", &ok);
    if (!ok || funcName.isEmpty()) return;

    QString returnType = QInputDialog::getText(this, "Add Function", "Return type:", QLineEdit::Normal, "void", &ok);
    if (!ok || returnType.isEmpty()) return;

    // 询问是否有参数
    int rc = QMessageBox::question(this, "Parameters", "Do you want to add parameters?", QMessageBox::Yes | QMessageBox::No);
    
    QString paramsStr = "";
    QJsonArray paramsArr;
    
    if (rc == QMessageBox::Yes) {
        // 让用户输入参数字符串
        paramsStr = QInputDialog::getText(this, "Add Function", 
            "Parameters (format: type1 name1, type2 name2):", 
            QLineEdit::Normal, "", &ok);
        
        if (ok && !paramsStr.isEmpty()) {
            // 解析参数字符串
            QStringList paramList = paramsStr.split(",");
            for (const QString &p : paramList) {
                QStringList parts = p.trimmed().split(" ");
                if (parts.count() >= 2) {
                    ParamMeta pm;
                    pm.type = parts[0];
                    pm.name = parts[1];
                    pm.defaultValue = "";
                    pm.isConstRef = false;
                    paramsArr.append(QJsonObject{{"type", pm.type}, {"name", pm.name}, {"defaultValue", pm.defaultValue}, {"isConstRef", pm.isConstRef}});
                }
            }
        }
    }

    // 询问是否是const函数
    bool isConst = false;
    rc = QMessageBox::question(this, "Const Method?", "Should the method be const?", QMessageBox::Yes | QMessageBox::No);
    if (rc == QMessageBox::Yes) isConst = true;

    // 添加到表格
    int row = ui->functionsTable->rowCount();
    ui->functionsTable->insertRow(row);
    QTableWidgetItem *nameItem = new QTableWidgetItem(funcName);
    nameItem->setData(RoleFunctionParameters, QJsonDocument(paramsArr).toJson(QJsonDocument::Compact));
    ui->functionsTable->setItem(row, 0, nameItem);
    ui->functionsTable->setItem(row, 1, new QTableWidgetItem(returnType));
    ui->functionsTable->setItem(row, 2, new QTableWidgetItem(parameterSummary(paramsArr)));
    ui->functionsTable->setItem(row, 3, new QTableWidgetItem(boolToYesNo(isConst)));
    ui->functionsTable->setItem(row, 4, new QTableWidgetItem("No"));
    ui->functionsTable->setItem(row, 5, new QTableWidgetItem("No"));
    ui->functionsTable->setItem(row, 6, new QTableWidgetItem("public"));
    ui->functionsTable->selectRow(row);
    loadParameterEditorFromSelectedFunction();
    loadFunctionOptionsFromSelectedFunction();
    refreshPreview();
    persistProjectModel();
}

void MainWindow::onAddMemberClicked()
{
    if (!m_currentClassItem) {
        QMessageBox::warning(this, "Tip", "Please select a Class first.");
        return;
    }
    
    bool ok;
    QString type = QInputDialog::getText(this, "Add Member", "Member type (e.g. int, QString):", QLineEdit::Normal, "int", &ok);
    if (!ok || type.isEmpty()) return;
    
    QString name = QInputDialog::getText(this, "Add Member", "Member name (e.g. m_count):", QLineEdit::Normal, "", &ok);
    if (!ok || name.isEmpty()) return;
    
    QString defaultValue = QInputDialog::getText(this, "Add Member", "Default value (optional):", QLineEdit::Normal, "", &ok);
    
    // 添加到表格
    int row = ui->membersTable->rowCount();
    ui->membersTable->insertRow(row);
    ui->membersTable->setItem(row, 0, new QTableWidgetItem(type));
    ui->membersTable->setItem(row, 1, new QTableWidgetItem(name));
    ui->membersTable->setItem(row, 2, new QTableWidgetItem(defaultValue));
    ui->membersTable->setItem(row, 3, new QTableWidgetItem("private"));
    refreshPreview();
    persistProjectModel();
}

void MainWindow::onDeleteItemClicked()
{
    // 检查哪个表格有选中项
    if (ui->functionsTable->currentRow() >= 0) {
        ui->functionsTable->removeRow(ui->functionsTable->currentRow());
        loadParameterEditorFromSelectedFunction();
        refreshPreview();
        persistProjectModel();
        return;
    }

    if (ui->parametersTable->currentRow() >= 0) {
        onDeleteParameterClicked();
        return;
    }
    
    if (ui->membersTable->currentRow() >= 0) {
        ui->membersTable->removeRow(ui->membersTable->currentRow());
        refreshPreview();
        persistProjectModel();
        return;
    }
    
    QMessageBox::information(this, "Tip", "Please select an item to delete from the tables.");
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
            ui->logEdit->append("No existing project metadata found. Started a new model.");
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
}

void MainWindow::onFunctionFlagsChanged()
{
    syncSelectedFunctionFlags();
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
    ui->parametersTable->setItem(row, 0, new QTableWidgetItem("int"));
    ui->parametersTable->setItem(row, 1, new QTableWidgetItem(QString("arg%1").arg(row + 1)));
    ui->parametersTable->setItem(row, 2, new QTableWidgetItem(QString()));
    ui->parametersTable->setItem(row, 3, new QTableWidgetItem("No"));
    ui->parametersTable->setCurrentCell(row, 0);
    syncSelectedFunctionParameters();
}

void MainWindow::onDeleteParameterClicked()
{
    if (ui->parametersTable->currentRow() < 0) {
        QMessageBox::information(this, "Tip", "Please select a parameter first.");
        return;
    }

    ui->parametersTable->removeRow(ui->parametersTable->currentRow());
    syncSelectedFunctionParameters();
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
        ui->parametersTable->setItem(index, 0, new QTableWidgetItem(param.type));
        ui->parametersTable->setItem(index, 1, new QTableWidgetItem(param.name));
        ui->parametersTable->setItem(index, 2, new QTableWidgetItem(param.defaultValue));
        ui->parametersTable->setItem(index, 3, new QTableWidgetItem(param.isConstRef ? "Yes" : "No"));
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
        param.defaultValue = ui->parametersTable->item(index, 2) ? ui->parametersTable->item(index, 2)->text().trimmed() : QString();
        param.isConstRef = ui->parametersTable->item(index, 3) && ui->parametersTable->item(index, 3)->text().compare("Yes", Qt::CaseInsensitive) == 0;
        parameters.append(param);
    }

    if (QTableWidgetItem *nameItem = ui->functionsTable->item(row, 0)) {
        nameItem->setData(RoleFunctionParameters, QJsonDocument(parameterArrayFromMetaList(parameters)).toJson(QJsonDocument::Compact));
    }

    const QSignalBlocker blocker(ui->functionsTable);
    if (QTableWidgetItem *paramsItem = ui->functionsTable->item(row, 2)) {
        paramsItem->setText(parameterSummary(parameters));
    } else {
        ui->functionsTable->setItem(row, 2, new QTableWidgetItem(parameterSummary(parameters)));
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

    ui->treeView->expandAll();
    ui->logEdit->append(QString("Loaded project metadata from: %1").arg(QDir::toNativeSeparators(metadataPath)));
    statusBar()->showMessage("Project model loaded", 3000);
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
