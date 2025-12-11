#include "devicefilebrowser.h"
#include <QHeaderView>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include "../QtScrcpyCore/include/adbprocess.h"
#include "../QtScrcpyCore/src/adb/adbprocessimpl.h"

DeviceFileBrowser::DeviceFileBrowser(const QString &serial, const QString &initialPath, QWidget *parent, bool allowFileSelection)
    : QDialog(parent), m_serial(serial), m_currentPath(initialPath), m_allowFileSelection(allowFileSelection)
{
    setWindowTitle(tr("Browse Device Files - Double click folder to expand, double click file to select"));
    setMinimumSize(600, 500);
    resize(700, 550);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // 路径显示和编辑
    QHBoxLayout *pathLayout = new QHBoxLayout;
    pathLayout->addWidget(new QLabel(tr("Current Path:")));
    m_pathEdit = new QLineEdit;
    m_pathEdit->setText(m_currentPath);
    pathLayout->addWidget(m_pathEdit);
    m_refreshBtn = new QPushButton(tr("Refresh"));
    pathLayout->addWidget(m_refreshBtn);
    mainLayout->addLayout(pathLayout);

    m_pathLabel = new QLabel(tr("Loading..."));
    mainLayout->addWidget(m_pathLabel);

    // 文件树
    m_fileTree = new QTreeWidget;
    m_fileTree->setHeaderLabels(QStringList() << tr("Files and Folders"));
    m_fileTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_fileTree->setRootIsDecorated(true);
    mainLayout->addWidget(m_fileTree);

    // 按钮布局
    QHBoxLayout *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch();
    m_okBtn = new QPushButton(tr("OK"));
    m_cancelBtn = new QPushButton(tr("Cancel"));
    buttonLayout->addWidget(m_okBtn);
    buttonLayout->addWidget(m_cancelBtn);
    mainLayout->addLayout(buttonLayout);

    // 连接信号
    connect(m_fileTree, &QTreeWidget::itemDoubleClicked, this, &DeviceFileBrowser::onItemDoubleClicked);
    connect(m_fileTree, &QTreeWidget::itemExpanded, this, &DeviceFileBrowser::onItemExpanded);
    connect(m_fileTree, &QTreeWidget::itemCollapsed, this, &DeviceFileBrowser::onItemCollapsed);
    connect(m_refreshBtn, &QPushButton::clicked, [this]() {
        updateFileList(m_pathEdit->text().trimmed());
    });
    connect(m_pathEdit, &QLineEdit::returnPressed, [this]() {
        updateFileList(m_pathEdit->text().trimmed());
    });
    connect(m_okBtn, &QPushButton::clicked, this, &DeviceFileBrowser::onOkClicked);
    connect(m_cancelBtn, &QPushButton::clicked, this, &DeviceFileBrowser::onCancelClicked);

    // 初始化文件列表
    updateFileList(m_currentPath);
}

bool DeviceFileBrowser::isDirectory(const QString &path)
{
    QProcess testProcess;
    QStringList testArgs;
    if (!m_serial.isEmpty()) {
        testArgs << "-s" << m_serial;
    }
    // 使用 stat 命令检测，更可靠
    testArgs << "shell" << "stat" << "-c" << "%F" << path;
    
    // Use the same adb path as the rest of the application to avoid PATH issues.
    testProcess.start(AdbProcessImpl::getAdbPath(), testArgs);
    if (!testProcess.waitForFinished(3000)) {
        // 超时，尝试使用 test -d
        testProcess.kill();
        testProcess.waitForFinished(1000);
        
        testArgs.clear();
        if (!m_serial.isEmpty()) {
            testArgs << "-s" << m_serial;
        }
        testArgs << "shell" << "test" << "-d" << path;
        testProcess.start(AdbProcessImpl::getAdbPath(), testArgs);
        testProcess.waitForFinished(3000);
        return testProcess.exitCode() == 0;
    }
    
    QString output = QString::fromUtf8(testProcess.readAllStandardOutput()).trimmed();
    // stat 命令返回 "directory" 表示是目录
    return output.contains("directory", Qt::CaseInsensitive);
}

QString DeviceFileBrowser::getFullPath(QTreeWidgetItem *item)
{
    if (!item || item == m_fileTree->invisibleRootItem()) {
        return m_currentPath;
    }
    
    // 构建完整路径：从根节点到当前节点的所有路径部分
    QStringList pathParts;
    QTreeWidgetItem *current = item;
    
    while (current && current != m_fileTree->invisibleRootItem()) {
        QString name = current->text(0);
        if (!name.isEmpty() && name != "..") {
            pathParts.prepend(name);
        }
        current = current->parent();
    }
    
    // 构建完整路径
    QString fullPath = m_currentPath;
    for (const QString &part : pathParts) {
        if (fullPath.endsWith("/")) {
            fullPath += part;
        } else {
            fullPath += "/" + part;
        }
    }
    
    return fullPath;
}

QTreeWidgetItem *DeviceFileBrowser::createTreeItem(const QString &name, bool isDir, QTreeWidgetItem *parent)
{
    QTreeWidgetItem *item;
    if (parent) {
        item = new QTreeWidgetItem(parent);
    } else {
        item = new QTreeWidgetItem(m_fileTree);
    }
    item->setText(0, name);
    item->setData(0, Qt::UserRole, isDir); // 存储是否为目录
    
    if (isDir) {
        // 文件夹显示展开图标，默认不展开
        item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        item->setExpanded(false);
        // 添加一个占位子项，表示可以展开
        QTreeWidgetItem *placeholder = new QTreeWidgetItem(item);
        placeholder->setText(0, tr("Loading..."));
        placeholder->setData(0, Qt::UserRole, QVariant());
        placeholder->setFlags(Qt::NoItemFlags);
    } else {
        // 文件不需要展开图标
        item->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
    }
    
    return item;
}

void DeviceFileBrowser::updateFileList(const QString &path)
{
    if (path.isEmpty()) {
        m_currentPath = "/sdcard/";
    } else {
        m_currentPath = path;
        if (!m_currentPath.endsWith("/") && !m_currentPath.endsWith("..")) {
            m_currentPath += "/";
        }
    }

    m_pathEdit->setText(m_currentPath);
    m_pathLabel->setText(tr("Loading files from: %1").arg(m_currentPath));
    m_fileTree->clear();

    // 使用 ADB 获取文件列表
    qsc::AdbProcess tempAdb;
    QStringList files = tempAdb.listDeviceFiles(m_serial, m_currentPath);

    // 添加返回上一级选项
    if (m_currentPath != "/" && m_currentPath != "/sdcard/" && !m_currentPath.endsWith("/sdcard")) {
        QTreeWidgetItem *parentItem = new QTreeWidgetItem(m_fileTree);
        parentItem->setText(0, "[..] (Go to parent directory)");
        parentItem->setData(0, Qt::UserRole, false);
        parentItem->setForeground(0, QColor(Qt::blue));
        parentItem->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
    }

    // 添加文件和文件夹
    for (const QString &file : files) {
        QString fullPath = m_currentPath + file;
        bool isDir = isDirectory(fullPath);
        createTreeItem(file, isDir, nullptr);
    }

    if (files.isEmpty() && m_fileTree->topLevelItemCount() == 0) {
        QTreeWidgetItem *item = new QTreeWidgetItem(m_fileTree);
        item->setText(0, tr("(Empty directory)"));
        item->setData(0, Qt::UserRole, QVariant());
        item->setFlags(Qt::NoItemFlags);
        item->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
    }

    m_pathLabel->setText(tr("Current path: %1 (%2 items)").arg(m_currentPath).arg(files.size()));
}

void DeviceFileBrowser::loadFolderContents(QTreeWidgetItem *parentItem)
{
    if (!parentItem) {
        return;
    }
    
    // 移除占位子项
    while (parentItem->childCount() > 0) {
        QTreeWidgetItem *child = parentItem->child(0);
        if (child->data(0, Qt::UserRole).isValid() == false || child->flags() == Qt::NoItemFlags) {
            delete child;
        } else {
            break;
        }
    }
    
    // 获取文件夹路径
    QString folderPath = getFullPath(parentItem);
    if (folderPath.isEmpty() || folderPath == "..") {
        return;
    }
    
    if (!folderPath.endsWith("/")) {
        folderPath += "/";
    }
    
    // 使用 ADB 获取文件夹内容
    qsc::AdbProcess tempAdb;
    QStringList files = tempAdb.listDeviceFiles(m_serial, folderPath);
    
    // 添加文件和子文件夹
    for (const QString &file : files) {
        QString fullPath = folderPath + file;
        bool isDir = isDirectory(fullPath);
        createTreeItem(file, isDir, parentItem);
    }
}

void DeviceFileBrowser::onItemExpanded(QTreeWidgetItem *item)
{
    if (!item) {
        return;
    }
    
    // 当文件夹展开时，加载其内容
    bool isDir = item->data(0, Qt::UserRole).toBool();
    if (isDir) {
        // 检查是否需要加载内容（是否有占位子项）
        if (item->childCount() == 0 || 
            (item->childCount() == 1 && !item->child(0)->data(0, Qt::UserRole).isValid())) {
            loadFolderContents(item);
        }
    }
}

void DeviceFileBrowser::onItemCollapsed(QTreeWidgetItem *item)
{
    if (!item) {
        return;
    }
    
}

void DeviceFileBrowser::onItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);
    if (!item || item == m_fileTree->invisibleRootItem()) {
        return;
    }

    QString itemText = item->text(0);
    if (itemText.startsWith("[..]")) {
        // 返回上一级
        QString parentPath = m_currentPath;
        if (parentPath.endsWith("/")) {
            parentPath = parentPath.left(parentPath.length() - 1);
        }
        int lastSlash = parentPath.lastIndexOf("/");
        if (lastSlash > 0) {
            parentPath = parentPath.left(lastSlash + 1);
        } else {
            parentPath = "/";
        }
        updateFileList(parentPath);
        return;
    }
    
    // 检查是否为目录
    QVariant dirVariant = item->data(0, Qt::UserRole);
    if (!dirVariant.isValid()) {
        return; // 占位项或其他无效项
    }
    
    bool isDir = dirVariant.toBool();
    
    if (isDir) {
        // 是文件夹，展开/折叠
        if (item->isExpanded()) {
            item->setExpanded(false);
        } else {
            item->setExpanded(true);
            // 展开时会触发 onItemExpanded，在那里加载内容
        }
    } else {
        // 是文件
        if (!m_allowFileSelection) {
            QMessageBox::warning(this, tr("Invalid Selection"), tr("Please select a directory"));
            return;
        }
        QString fullPath = getFullPath(item);
        m_selectedPath = fullPath;
        accept();
    }
}

void DeviceFileBrowser::onOkClicked()
{
    QTreeWidgetItem *item = m_fileTree->currentItem();
    if (!item || item == m_fileTree->invisibleRootItem()) {
        QMessageBox::warning(this, tr("No Selection"), tr("Please select a file or directory"));
        return;
    }

    QString itemText = item->text(0);
    if (itemText.startsWith("[..]")) {
        QMessageBox::warning(this, tr("Invalid Selection"), tr("Please select a file or directory, not navigation"));
        return;
    }

    // 检查有效性
    QVariant dirVariant = item->data(0, Qt::UserRole);
    if (!dirVariant.isValid()) {
        QMessageBox::warning(this, tr("Invalid Selection"), tr("Please select a valid item"));
        return;
    }

    bool isDir = dirVariant.toBool();
    if (!isDir && !m_allowFileSelection) {
        QMessageBox::warning(this, tr("Invalid Selection"), tr("Please select a directory"));
        return;
    }

    // 允许选择文件或目录（受 m_allowFileSelection 控制）
    QString fullPath = getFullPath(item);
    m_selectedPath = fullPath;
    accept();
}

void DeviceFileBrowser::onRefreshClicked()
{
    updateFileList(m_currentPath);
}

void DeviceFileBrowser::onCancelClicked()
{
    reject();
}
