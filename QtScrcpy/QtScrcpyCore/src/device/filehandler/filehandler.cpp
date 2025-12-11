#include "filehandler.h"

FileHandler::FileHandler(QObject *parent) : QObject(parent)
{
}

FileHandler::~FileHandler() {}

void FileHandler::onPushFileRequest(const QString &serial, const QString &file, const QString &devicePath)
{
    qsc::AdbProcess* adb = new qsc::AdbProcess;
    bool isApk = false;
    bool isPull = false;
    connect(adb, &qsc::AdbProcess::adbProcessResult, this, [this, adb, isApk, isPull](qsc::AdbProcess::ADB_EXEC_RESULT processResult) {
        onAdbProcessResult(adb, isApk, isPull, processResult);
    });

    adb->push(serial, file, devicePath);
}

void FileHandler::onPullFileRequest(const QString &serial, const QString &devicePath, const QString &localPath)
{
    qsc::AdbProcess* adb = new qsc::AdbProcess;
    bool isApk = false;
    bool isPull = true;
    connect(adb, &qsc::AdbProcess::adbProcessResult, this, [this, adb, isApk, isPull](qsc::AdbProcess::ADB_EXEC_RESULT processResult) {
        onAdbProcessResult(adb, isApk, isPull, processResult);
    });

    adb->pull(serial, devicePath, localPath);
}

void FileHandler::onInstallApkRequest(const QString &serial, const QString &apkFile)
{
    qsc::AdbProcess* adb = new qsc::AdbProcess;
    bool isApk = true;
    bool isPull = false;
    connect(adb, &qsc::AdbProcess::adbProcessResult, this, [this, adb, isApk, isPull](qsc::AdbProcess::ADB_EXEC_RESULT processResult) {
        onAdbProcessResult(adb, isApk, isPull, processResult);
    });

    adb->install(serial, apkFile);
}

void FileHandler::onAdbProcessResult(qsc::AdbProcess *adb, bool isApk, bool isPull, qsc::AdbProcess::ADB_EXEC_RESULT processResult)
{
    switch (processResult) {
    case qsc::AdbProcess::AER_ERROR_START:
    case qsc::AdbProcess::AER_ERROR_EXEC:
    case qsc::AdbProcess::AER_ERROR_MISSING_BINARY:
        emit fileHandlerResult(FAR_ERROR_EXEC, isApk, isPull);
        adb->deleteLater();
        break;
    case qsc::AdbProcess::AER_SUCCESS_EXEC:
        emit fileHandlerResult(FAR_SUCCESS_EXEC, isApk, isPull);
        adb->deleteLater();
        break;
    default:
        break;
    }
}
