#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTreeView>
#include <QStandardItemModel>
#include <QAction>
#include <QMenu>
#include "MetaTypes.h" // 引用 GenCore 的定义

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class GeneratorEngine; // 前向声明

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onActionNewClass();
    void onActionNewFunction();
    void onActionSaveTemplate();
    void onActionGenerate();
    void onActionGenerateAll();
    void onActionOpenProject();
    void onActionAbout();
    void onTreeSelectionChanged();
    
    // 新增：右键菜单相关
    void onCustomContextMenuRequested(const QPoint &point);
    void onAddNewClassFromContextMenu();
    
    // 新增：属性面板相关
    void onAddFunctionClicked();
    void onAddMemberClicked();
    void onDeleteItemClicked();
    void onSaveClassClicked();
    void onFunctionSelectionChanged();
    void onAddParameterClicked();
    void onDeleteParameterClicked();
    void onFunctionFlagsChanged();
    void clearClassInfoPanel();

private:
    Ui::MainWindow *ui;
    QStandardItemModel *m_model;
    GeneratorEngine *m_engine;
    QString m_projectRootPath; // 当前项目路径
    QString m_templateRootPath;
    QMenu *m_contextMenu;
    QStandardItem *m_currentClassItem; // 当前选中的类节点

    // 初始化默认的 6 大业务域
    void initializeDefaultDomains();
    
    // 获取当前选中节点对应的 ClassMeta
    ClassMeta getCurrentClassMeta() const;
    
    // 更新右侧面板显示类信息
    void updateClassInfoPanel(QStandardItem *classItem);
    
    // 从面板获取ClassMeta并保存
    ClassMeta getClassMetaFromPanel() const;
    
    // 保存类信息到树节点
    void saveClassInfoToNode(QStandardItem *classItem, const ClassMeta &meta);

    // 刷新输出路径在界面中的显示
    void updateOutputPathDisplay();

    // 根据当前编辑内容刷新代码预览
    void refreshPreview();

    // 将当前选中函数的参数加载到参数编辑表
    void loadParameterEditorFromSelectedFunction();

    // 将参数编辑表回写到当前选中函数
    void syncSelectedFunctionParameters();

    // 加载当前选中函数的属性勾选状态
    void loadFunctionOptionsFromSelectedFunction();

    // 将属性勾选状态回写到当前选中函数
    void syncSelectedFunctionFlags();

    // 提取指定函数行的参数信息
    QList<ParamMeta> functionParametersFromRow(int row) const;

    // 当前输出目录下的项目元数据文件路径
    QString projectMetadataFilePath() const;

    // 将当前模型持久化到输出目录
    bool persistProjectModel();

    // 从输出目录加载项目模型
    bool loadProjectModel(const QString &rootPath);
};
#endif // MAINWINDOW_H