#ifndef DEVICEFILEBROWSER_H
#define DEVICEFILEBROWSER_H

#include <QDialog>
#include <QTreeWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>

class DeviceFileBrowser : public QDialog
{
    Q_OBJECT

public:
    explicit DeviceFileBrowser(const QString &serial, const QString &initialPath, QWidget *parent = nullptr, bool allowFileSelection = true);
    QString getSelectedPath() const { return m_selectedPath; }

private slots:
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);
    void onItemExpanded(QTreeWidgetItem *item);
    void onItemCollapsed(QTreeWidgetItem *item);
    void onRefreshClicked();
    void onCancelClicked();
    void onOkClicked();
    void updateFileList(const QString &path);
    void loadFolderContents(QTreeWidgetItem *parentItem);

private:
    bool isDirectory(const QString &path);
    QString getFullPath(QTreeWidgetItem *item);
    QTreeWidgetItem *createTreeItem(const QString &name, bool isDir, QTreeWidgetItem *parent = nullptr);

private:
    QString m_serial;
    QString m_currentPath;
    QString m_selectedPath;
    QTreeWidget *m_fileTree;
    QPushButton *m_refreshBtn;
    QPushButton *m_okBtn;
    QPushButton *m_cancelBtn;
    QLabel *m_pathLabel;
    QLineEdit *m_pathEdit;
    bool m_allowFileSelection = true;
};

#endif // DEVICEFILEBROWSER_H
