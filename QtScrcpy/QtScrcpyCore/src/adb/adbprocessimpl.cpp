#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
#include <QRegExp>
#else
#include <QRegularExpression>
#endif

#include "adbprocessimpl.h"

QString AdbProcessImpl::s_adbPath = "";
extern QString g_adbPath;

AdbProcessImpl::AdbProcessImpl(QObject *parent) : QProcess(parent)
{
    setProcessChannelMode(QProcess::MergedChannels);
    initSignals();
}

AdbProcessImpl::~AdbProcessImpl()
{
    if (isRuning()) {
        close();
    }
}

const QString &AdbProcessImpl::getAdbPath()
{
    if (s_adbPath.isEmpty()) {
        QStringList potentialPaths;
        potentialPaths << QString::fromLocal8Bit(qgetenv("QTSCRCPY_ADB_PATH")) << g_adbPath
#ifdef Q_OS_WIN32
                       << QCoreApplication::applicationDirPath() + "/adb.exe";
#else
                       << QCoreApplication::applicationDirPath() + "/adb";
#endif

        for (const QString &path : potentialPaths) {
            QFileInfo fileInfo(path);
            if (!path.isEmpty() && fileInfo.isFile()) {
                s_adbPath = path;
                break;
            }
        }

        if (s_adbPath.isEmpty()) {
            // 如果所有路径都不满足条件，可以选择抛出异常或设置默认值
            qWarning() << "ADB路径未找到";
        } else {
            qInfo("adb path: %s", QDir(s_adbPath).absolutePath().toUtf8().data());
        }
    }
    return s_adbPath;
}

void AdbProcessImpl::initSignals()
{
    // aboutToQuit not exit event loop, so deletelater is ok
    //connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &AdbProcessImpl::deleteLater);

    connect(this, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        if (NormalExit == exitStatus && 0 == exitCode) {
            emit adbProcessImplResult(qsc::AdbProcess::AER_SUCCESS_EXEC);
        } else {
            //P7C0218510000537        unauthorized ,手机端此时弹出调试认证，要允许调试
            emit adbProcessImplResult(qsc::AdbProcess::AER_ERROR_EXEC);
        }
        qDebug() << "adb return " << exitCode << "exit status " << exitStatus;
        m_transferDirection = TransferDirection::None;
        m_progressBuffer.clear();
        m_lastProgress = -1;
        m_totalBytes = 0;
        m_lastBytes = -1;
    });

    connect(this, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (QProcess::FailedToStart == error) {
            emit adbProcessImplResult(qsc::AdbProcess::AER_ERROR_MISSING_BINARY);
        } else {
            emit adbProcessImplResult(qsc::AdbProcess::AER_ERROR_START);
            QString err = QString("qprocess start error:%1 %2").arg(program()).arg(arguments().join(" "));
            qCritical() << err.toStdString().c_str();
            emit transferLog(err);
        }
        m_transferDirection = TransferDirection::None;
        m_progressBuffer.clear();
        m_lastProgress = -1;
        m_totalBytes = 0;
        m_lastBytes = -1;
    });

    connect(this, &QProcess::readyReadStandardError, this, [this]() {
        QString tmpRaw = QString::fromUtf8(readAllStandardError());
        m_errorOutput += tmpRaw;
        if (!tmpRaw.isEmpty()) {
            qWarning() << QString("AdbProcessImpl::error:%1").arg(tmpRaw).toStdString().data();
            emit transferLog(tmpRaw);
        }
        parseTransferProgress(tmpRaw);
    });

    connect(this, &QProcess::readyReadStandardOutput, this, [this]() {
        QString tmpRaw = QString::fromUtf8(readAllStandardOutput());
        m_standardOutput += tmpRaw;
        if (!tmpRaw.isEmpty()) {
            qInfo() << QString("AdbProcessImpl::out:%1").arg(tmpRaw).toStdString().data();
            emit transferLog(tmpRaw);
        }
        parseTransferProgress(tmpRaw);
    });

    connect(this, &QProcess::started, this, [this]() { emit adbProcessImplResult(qsc::AdbProcess::AER_SUCCESS_START); });
}

void AdbProcessImpl::execute(const QString &serial, const QStringList &args)
{
    m_standardOutput = "";
    m_errorOutput = "";
    m_progressBuffer.clear();
    m_lastProgress = -1;
    m_totalBytes = 0;
    m_lastBytes = -1;
    if (m_transferDirection != TransferDirection::Push && m_transferDirection != TransferDirection::Pull) {
        m_transferDirection = TransferDirection::None;
    }
    QStringList adbArgs;
    if (!serial.isEmpty()) {
        adbArgs << "-s" << serial;
    }
    adbArgs << args;
    qDebug() << getAdbPath() << adbArgs.join(" ");
    emit transferLog(QString("exec: %1 %2").arg(getAdbPath()).arg(adbArgs.join(" ")));

#ifdef Q_OS_WIN32
    start(getAdbPath(), adbArgs);
#else
    bool useScript = (m_transferDirection == TransferDirection::Push || m_transferDirection == TransferDirection::Pull);
    QString scriptCmd = "/usr/bin/script";
    QFileInfo scriptInfo(scriptCmd);
    if (useScript && scriptInfo.exists()) {
        QStringList scriptArgs;
        scriptArgs << "-q" << "/dev/null" << getAdbPath();
        scriptArgs.append(adbArgs);
        emit transferLog(QString("exec via script pty: %1 %2").arg(scriptCmd).arg(scriptArgs.join(" ")));
        start(scriptCmd, scriptArgs);
    } else {
        if (useScript && !scriptInfo.exists()) {
            emit transferLog(QString("script not found at %1, fallback without pty").arg(scriptCmd));
        }
        start(getAdbPath(), adbArgs);
    }
#endif
}

bool AdbProcessImpl::isRuning()
{
    if (QProcess::NotRunning == state()) {
        return false;
    } else {
        return true;
    }
}

void AdbProcessImpl::setShowTouchesEnabled(const QString &serial, bool enabled)
{
    QStringList adbArgs;
    adbArgs << "shell"
            << "settings"
            << "put"
            << "system"
            << "show_touches";
    adbArgs << (enabled ? "1" : "0");
    execute(serial, adbArgs);
}

QStringList AdbProcessImpl::getDevicesSerialFromStdOut()
{
    // get devices serial by adb devices
    QStringList serials;
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QRegExp lineExp("\r\n|\n");
    QRegExp tExp("\t");
#else
    QRegularExpression lineExp("\r\n|\n");
    QRegularExpression tExp("\t");
#endif
    QStringList devicesInfoList = m_standardOutput.split(lineExp);
    for (QString deviceInfo : devicesInfoList) {
        QStringList deviceInfos = deviceInfo.split(tExp);
        if (2 == deviceInfos.count() && 0 == deviceInfos[1].compare("device")) {
            serials << deviceInfos[0];
        }
    }
    return serials;
}

QString AdbProcessImpl::getDeviceIPFromStdOut()
{
    QString ip = "";
    QString strIPExp = "inet addr:[\\d.]*";
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QRegExp ipRegExp(strIPExp, Qt::CaseInsensitive);
    if (ipRegExp.indexIn(m_standardOutput) != -1) {
        ip = ipRegExp.cap(0);
        ip = ip.right(ip.size() - 10);
    }
#else
    QRegularExpression ipRegExp(strIPExp, QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = ipRegExp.match(m_standardOutput);
    if (match.hasMatch()) {
        ip = match.captured(0);
        ip = ip.right(ip.size() - 10);
    }
#endif

    return ip;
}

QString AdbProcessImpl::getDeviceIPByIpFromStdOut()
{
    QString ip = "";

    QString strIPExp = "wlan0    inet [\\d.]*";
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QRegExp ipRegExp(strIPExp, Qt::CaseInsensitive);
    if (ipRegExp.indexIn(m_standardOutput) != -1) {
        ip = ipRegExp.cap(0);
        ip = ip.right(ip.size() - 14);
    }
#else
    QRegularExpression ipRegExp(strIPExp, QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = ipRegExp.match(m_standardOutput);
    if (match.hasMatch()) {
        ip = match.captured(0);
        ip = ip.right(ip.size() - 14);
    }
#endif
    qDebug() << "get ip: " << ip;
    return ip;
}

QStringList AdbProcessImpl::listDeviceFiles(const QString &serial, const QString &path)
{
    QStringList files;
    
    // 使用同步执行方式获取文件列表
    QProcess process;
    QStringList fullArgs;
    if (!serial.isEmpty()) {
        fullArgs << "-s" << serial;
    }
    fullArgs << "shell" << "ls" << "-1" << path;
    
    process.start(getAdbPath(), fullArgs);
    if (!process.waitForFinished(5000)) {
        qWarning() << "Failed to list device files, timeout";
        return files;
    }
    
    if (process.exitCode() != 0) {
        qWarning() << "Failed to list device files:" << QString::fromUtf8(process.readAllStandardError());
        return files;
    }
    
    QString output = QString::fromUtf8(process.readAllStandardOutput());
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QRegExp lineExp("\r\n|\n");
#if (QT_VERSION < QT_VERSION_CHECK(5, 15, 0))
    QStringList lines = output.split(lineExp, QString::SkipEmptyParts);
#else
    QStringList lines = output.split(lineExp, Qt::SkipEmptyParts);
#endif
#else
    QRegularExpression lineExp("\r\n|\n");
    QStringList lines = output.split(lineExp, Qt::SkipEmptyParts);
#endif
    
    for (const QString &line : lines) {
        QString trimmed = line.trimmed();
        if (!trimmed.isEmpty() && !trimmed.startsWith("total") && trimmed != "." && trimmed != "..") {
            files << trimmed;
        }
    }
    
    return files;
}

QString AdbProcessImpl::getStdOut()
{
    return m_standardOutput;
}

QString AdbProcessImpl::getErrorOut()
{
    return m_errorOutput;
}

void AdbProcessImpl::forward(const QString &serial, quint16 localPort, const QString &deviceSocketName)
{
    m_transferDirection = TransferDirection::None;
    QStringList adbArgs;
    adbArgs << "forward";
    adbArgs << QString("tcp:%1").arg(localPort);
    adbArgs << QString("localabstract:%1").arg(deviceSocketName);
    execute(serial, adbArgs);
}

void AdbProcessImpl::forwardRemove(const QString &serial, quint16 localPort)
{
    m_transferDirection = TransferDirection::None;
    QStringList adbArgs;
    adbArgs << "forward";
    adbArgs << "--remove";
    adbArgs << QString("tcp:%1").arg(localPort);
    execute(serial, adbArgs);
}

void AdbProcessImpl::reverse(const QString &serial, const QString &deviceSocketName, quint16 localPort)
{
    m_transferDirection = TransferDirection::None;
    QStringList adbArgs;
    adbArgs << "reverse";
    adbArgs << QString("localabstract:%1").arg(deviceSocketName);
    adbArgs << QString("tcp:%1").arg(localPort);
    execute(serial, adbArgs);
}

void AdbProcessImpl::reverseRemove(const QString &serial, const QString &deviceSocketName)
{
    m_transferDirection = TransferDirection::None;
    QStringList adbArgs;
    adbArgs << "reverse";
    adbArgs << "--remove";
    adbArgs << QString("localabstract:%1").arg(deviceSocketName);
    execute(serial, adbArgs);
}

void AdbProcessImpl::push(const QString &serial, const QString &local, const QString &remote)
{
    m_transferDirection = TransferDirection::Push;
    m_progressBuffer.clear();
    m_lastProgress = -1;
    m_totalBytes = 0;
    m_lastBytes = -1;
    QStringList adbArgs;
    adbArgs << "push";
    adbArgs << "-p";
    adbArgs << local;
    adbArgs << remote;
    execute(serial, adbArgs);
}

void AdbProcessImpl::pull(const QString &serial, const QString &remote, const QString &local)
{
    m_transferDirection = TransferDirection::Pull;
    m_progressBuffer.clear();
    m_lastProgress = -1;
    m_totalBytes = 0;
    m_lastBytes = -1;
    QStringList adbArgs;
    adbArgs << "pull";
    adbArgs << "-p";
    adbArgs << remote;
    adbArgs << local;
    execute(serial, adbArgs);
}

void AdbProcessImpl::install(const QString &serial, const QString &local)
{
    m_transferDirection = TransferDirection::None;
    QStringList adbArgs;
    adbArgs << "install";
    adbArgs << "-r";
    adbArgs << local;
    execute(serial, adbArgs);
}

void AdbProcessImpl::removePath(const QString &serial, const QString &path)
{
    m_transferDirection = TransferDirection::None;
    QStringList adbArgs;
    adbArgs << "shell";
    adbArgs << "rm";
    adbArgs << path;
    execute(serial, adbArgs);
}

void AdbProcessImpl::parseTransferProgress(const QString &text)
{
    if (m_transferDirection == TransferDirection::None) {
        return;
    }

    // 追加到缓冲，替换回车为换行便于匹配完整数字
    m_progressBuffer.append(text);
    m_progressBuffer.replace('\r', '\n');

    int matchedValue = -1;
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QRegExp percentExp("(\\d+)%");
    int pos = 0;
    while ((pos = percentExp.indexIn(m_progressBuffer, pos)) != -1) {
        matchedValue = percentExp.cap(1).toInt();
        pos += percentExp.matchedLength();
    }
#else
    QRegularExpression percentExp("(\\d+)%");
    QRegularExpressionMatchIterator it = percentExp.globalMatch(m_progressBuffer);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        bool ok = false;
        int value = match.captured(1).toInt(&ok);
        if (ok) {
            matchedValue = value;
        }
    }
#endif

    // adb 常规输出没有百分比，只输出 "xx MB/s (sent/total)"
    if (matchedValue < 0) {
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        QRegExp bytesExp("\\((\\d+)\\s*/\\s*(\\d+)\\)");
        int bpos = 0;
        while ((bpos = bytesExp.indexIn(m_progressBuffer, bpos)) != -1) {
            bool ok1 = false, ok2 = false;
            qint64 current = bytesExp.cap(1).toLongLong(&ok1);
            qint64 total = bytesExp.cap(2).toLongLong(&ok2);
            if (ok1 && ok2 && total > 0) {
                m_totalBytes = total;
                m_lastBytes = current;
                matchedValue = static_cast<int>((current * 100) / total);
            }
            bpos += bytesExp.matchedLength();
        }
#else
        QRegularExpression bytesExp("\\((\\d+)\\s*/\\s*(\\d+)\\)");
        QRegularExpressionMatchIterator itBytes = bytesExp.globalMatch(m_progressBuffer);
        while (itBytes.hasNext()) {
            QRegularExpressionMatch m = itBytes.next();
            bool ok1 = false, ok2 = false;
            qint64 current = m.captured(1).toLongLong(&ok1);
            qint64 total = m.captured(2).toLongLong(&ok2);
            if (ok1 && ok2 && total > 0) {
                m_totalBytes = total;
                m_lastBytes = current;
                matchedValue = static_cast<int>((current * 100) / total);
            }
        }
#endif
    }

    // 如果只出现 "(xxxx bytes in ...)" 视为已完成
    if (matchedValue < 0) {
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        QRegExp finalBytesExp("\\((\\d+)\\s+bytes");
        if (finalBytesExp.indexIn(m_progressBuffer) != -1) {
            matchedValue = 100;
        }
#else
        QRegularExpression finalBytesExp("\\((\\d+)\\s+bytes");
        if (finalBytesExp.match(m_progressBuffer).hasMatch()) {
            matchedValue = 100;
        }
#endif
    }

    if (matchedValue < 0 || matchedValue > 100) {
        return;
    }

    // 去重避免重复发送
    if (matchedValue != m_lastProgress) {
        m_lastProgress = matchedValue;
        bool isDownload = (m_transferDirection == TransferDirection::Pull);
        emit transferProgress(isDownload, matchedValue);
        emit transferLog(QString("transfer progress: %1%").arg(matchedValue));
    }

    if (matchedValue == 100) {
        m_transferDirection = TransferDirection::None;
        m_progressBuffer.clear();
        m_lastProgress = -1;
        m_totalBytes = 0;
        m_lastBytes = -1;
    }
}
