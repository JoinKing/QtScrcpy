#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QKeyEvent>
#include <QRandomGenerator>
#include <QTime>
#include <QTimer>

#include "config.h"
#include "dialog.h"
#include "devicefilebrowser.h"
#include "ui_dialog.h"
#include "videoform.h"
#include "../groupcontroller/groupcontroller.h"

#ifdef Q_OS_WIN32
#include "../util/winutils.h"
#endif

QString s_keyMapPath = "";

const QString &getKeyMapPath()
{
    if (s_keyMapPath.isEmpty()) {
        s_keyMapPath = QString::fromLocal8Bit(qgetenv("QTSCRCPY_KEYMAP_PATH"));
        QFileInfo fileInfo(s_keyMapPath);
        if (s_keyMapPath.isEmpty() || !fileInfo.isDir()) {
            s_keyMapPath = QCoreApplication::applicationDirPath() + "/keymap";
        }
    }
    return s_keyMapPath;
}

Dialog::Dialog(QWidget *parent) : QWidget(parent), ui(new Ui::Widget), m_fileTransferAdb(nullptr), m_isFileTransferInProgress(false), m_isCurrentTransferDownload(false), m_isDownloadCancelling(false)
{
    ui->setupUi(this);
    initUI();
    
    // 初始化文件传输进度条
    ui->downloadProgressBar->setValue(0);
    ui->uploadProgressBar->setValue(0);
    ui->downloadProgressBar->setTextVisible(true);
    ui->uploadProgressBar->setTextVisible(true);

    updateBootConfig(true);

    on_useSingleModeCheck_clicked();
    on_updateDevice_clicked();

    connect(&m_autoUpdatetimer, &QTimer::timeout, this, &Dialog::on_updateDevice_clicked);
    if (ui->autoUpdatecheckBox->isChecked()) {
        m_autoUpdatetimer.start(5000);
    }

    connect(&m_adb, &qsc::AdbProcess::adbProcessResult, this, [this](qsc::AdbProcess::ADB_EXEC_RESULT processResult) {
        QString log = "";
        bool newLine = true;
        QStringList args = m_adb.arguments();

        switch (processResult) {
        case qsc::AdbProcess::AER_ERROR_START:
            break;
        case qsc::AdbProcess::AER_SUCCESS_START:
            log = "adb run";
            newLine = false;
            break;
        case qsc::AdbProcess::AER_ERROR_EXEC:
            //log = m_adb.getErrorOut();
            if (args.contains("ifconfig") && args.contains("wlan0")) {
                getIPbyIp();
            }
            break;
        case qsc::AdbProcess::AER_ERROR_MISSING_BINARY:
            log = "adb not found";
            break;
        case qsc::AdbProcess::AER_SUCCESS_EXEC:
            //log = m_adb.getStdOut();
            if (args.contains("devices")) {
                QStringList devices = m_adb.getDevicesSerialFromStdOut();
                ui->serialBox->clear();
                ui->connectedPhoneList->clear();
                for (auto &item : devices) {
                    ui->serialBox->addItem(item);
                    ui->connectedPhoneList->addItem(Config::getInstance().getNickName(item) + "-" + item);
                }
            } else if (args.contains("show") && args.contains("wlan0")) {
                QString ip = m_adb.getDeviceIPFromStdOut();
                if (ip.isEmpty()) {
                    log = "ip not find, connect to wifi?";
                    break;
                }
                ui->deviceIpEdt->setEditText(ip);
            } else if (args.contains("ifconfig") && args.contains("wlan0")) {
                QString ip = m_adb.getDeviceIPFromStdOut();
                if (ip.isEmpty()) {
                    log = "ip not find, connect to wifi?";
                    break;
                }
                ui->deviceIpEdt->setEditText(ip);
            } else if (args.contains("ip -o a")) {
                QString ip = m_adb.getDeviceIPByIpFromStdOut();
                if (ip.isEmpty()) {
                    log = "ip not find, connect to wifi?";
                    break;
                }
                ui->deviceIpEdt->setEditText(ip);
            }
            break;
        }
        if (!log.isEmpty()) {
            outLog(log, newLine);
        }
    });

    m_hideIcon = new QSystemTrayIcon(this);
    m_hideIcon->setIcon(QIcon(":/image/tray/logo.png"));
    m_menu = new QMenu(this);
    m_quit = new QAction(this);
    m_showWindow = new QAction(this);
    m_showWindow->setText(tr("show"));
    m_quit->setText(tr("quit"));
    m_menu->addAction(m_showWindow);
    m_menu->addAction(m_quit);
    m_hideIcon->setContextMenu(m_menu);
    m_hideIcon->show();
    connect(m_showWindow, &QAction::triggered, this, &Dialog::show);
    connect(m_quit, &QAction::triggered, this, [this]() {
        m_hideIcon->hide();
        qApp->quit();
    });
    connect(m_hideIcon, &QSystemTrayIcon::activated, this, &Dialog::slotActivated);

    connect(&qsc::IDeviceManage::getInstance(), &qsc::IDeviceManage::deviceConnected, this, &Dialog::onDeviceConnected);
    connect(&qsc::IDeviceManage::getInstance(), &qsc::IDeviceManage::deviceDisconnected, this, &Dialog::onDeviceDisconnected);
}

Dialog::~Dialog()
{
    qDebug() << "~Dialog()";
    updateBootConfig(false);
    qsc::IDeviceManage::getInstance().disconnectAllDevice();
    delete ui;
}

void Dialog::initUI()
{
    setAttribute(Qt::WA_DeleteOnClose);
    //setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint | Qt::WindowCloseButtonHint | Qt::CustomizeWindowHint);

    setWindowTitle(Config::getInstance().getTitle());
#ifdef Q_OS_LINUX
    // Set window icon (inherits from application icon set in main.cpp)
    // If application icon was set, this will use it automatically
    if (!qApp->windowIcon().isNull()) {
        setWindowIcon(qApp->windowIcon());
    }
#endif

#ifdef Q_OS_WIN32
    WinUtils::setDarkBorderToWindow((HWND)this->winId(), true);
#endif

    ui->bitRateEdit->setValidator(new QIntValidator(1, 99999, this));

    ui->maxSizeBox->addItem("640");
    ui->maxSizeBox->addItem("720");
    ui->maxSizeBox->addItem("1080");
    ui->maxSizeBox->addItem("1280");
    ui->maxSizeBox->addItem("1920");
    ui->maxSizeBox->addItem(tr("original"));

    ui->formatBox->addItem("mp4");
    ui->formatBox->addItem("mkv");

    ui->lockOrientationBox->addItem(tr("no lock"));
    ui->lockOrientationBox->addItem("0");
    ui->lockOrientationBox->addItem("90");
    ui->lockOrientationBox->addItem("180");
    ui->lockOrientationBox->addItem("270");
    ui->lockOrientationBox->setCurrentIndex(0);

    // 加载IP历史记录
    loadIpHistory();

    // 加载端口历史记录
    loadPortHistory();
    
    // 初始化文件传输UI（中文提示）
    ui->uploadDevicePathEdt->setPlaceholderText(tr("点击“选择文件/文件夹”选择本地路径"));
    ui->uploadTargetPathEdt->setPlaceholderText(tr("点击“选择设备目录”选择目标目录"));
    ui->downloadPathEdt->setPlaceholderText(tr("点击“浏览”选择设备文件"));

    // 为deviceIpEdt添加右键菜单
    if (ui->deviceIpEdt->lineEdit()) {
        ui->deviceIpEdt->lineEdit()->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(ui->deviceIpEdt->lineEdit(), &QWidget::customContextMenuRequested,
                this, &Dialog::showIpEditMenu);
    }
    
    // 为devicePortEdt添加右键菜单
    if (ui->devicePortEdt->lineEdit()) {
        ui->devicePortEdt->lineEdit()->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(ui->devicePortEdt->lineEdit(), &QWidget::customContextMenuRequested,
                this, &Dialog::showPortEditMenu);
    }
}

void Dialog::updateBootConfig(bool toView)
{
    if (toView) {
        UserBootConfig config = Config::getInstance().getUserBootConfig();

        if (config.bitRate == 0) {
            ui->bitRateBox->setCurrentText("Mbps");
        } else if (config.bitRate % 1000000 == 0) {
            ui->bitRateEdit->setText(QString::number(config.bitRate / 1000000));
            ui->bitRateBox->setCurrentText("Mbps");
        } else {
            ui->bitRateEdit->setText(QString::number(config.bitRate / 1000));
            ui->bitRateBox->setCurrentText("Kbps");
        }

        ui->maxSizeBox->setCurrentIndex(config.maxSizeIndex);
        ui->formatBox->setCurrentIndex(config.recordFormatIndex);
        ui->recordPathEdt->setText(config.recordPath);
        ui->lockOrientationBox->setCurrentIndex(config.lockOrientationIndex);
        ui->framelessCheck->setChecked(config.framelessWindow);
        ui->recordScreenCheck->setChecked(config.recordScreen);
        ui->notDisplayCheck->setChecked(config.recordBackground);
        ui->useReverseCheck->setChecked(config.reverseConnect);
        ui->fpsCheck->setChecked(config.showFPS);
        ui->alwaysTopCheck->setChecked(config.windowOnTop);
        ui->closeScreenCheck->setChecked(config.autoOffScreen);
        ui->stayAwakeCheck->setChecked(config.keepAlive);
        ui->useSingleModeCheck->setChecked(config.simpleMode);
        ui->autoUpdatecheckBox->setChecked(config.autoUpdateDevice);
        ui->showToolbar->setChecked(config.showToolbar);
    } else {
        UserBootConfig config;

        config.bitRate = getBitRate();
        config.maxSizeIndex = ui->maxSizeBox->currentIndex();
        config.recordFormatIndex = ui->formatBox->currentIndex();
        config.recordPath = ui->recordPathEdt->text();
        config.lockOrientationIndex = ui->lockOrientationBox->currentIndex();
        config.recordScreen = ui->recordScreenCheck->isChecked();
        config.recordBackground = ui->notDisplayCheck->isChecked();
        config.reverseConnect = ui->useReverseCheck->isChecked();
        config.showFPS = ui->fpsCheck->isChecked();
        config.windowOnTop = ui->alwaysTopCheck->isChecked();
        config.autoOffScreen = ui->closeScreenCheck->isChecked();
        config.framelessWindow = ui->framelessCheck->isChecked();
        config.keepAlive = ui->stayAwakeCheck->isChecked();
        config.simpleMode = ui->useSingleModeCheck->isChecked();
        config.autoUpdateDevice = ui->autoUpdatecheckBox->isChecked();
        config.showToolbar = ui->showToolbar->isChecked();

        // 保存当前IP到历史记录
        QString currentIp = ui->deviceIpEdt->currentText().trimmed();
        if (!currentIp.isEmpty()) {
            saveIpHistory(currentIp);
        }

        Config::getInstance().setUserBootConfig(config);
    }
}

void Dialog::execAdbCmd()
{
    if (checkAdbRun()) {
        return;
    }
    QString cmd = ui->adbCommandEdt->text().trimmed();
    outLog("adb " + cmd, false);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    m_adb.execute(ui->serialBox->currentText().trimmed(), cmd.split(" ", Qt::SkipEmptyParts));
#else
    m_adb.execute(ui->serialBox->currentText().trimmed(), cmd.split(" ", QString::SkipEmptyParts));
#endif
}

void Dialog::delayMs(int ms)
{
    QTime dieTime = QTime::currentTime().addMSecs(ms);

    while (QTime::currentTime() < dieTime) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }
}

QString Dialog::getGameScript(const QString &fileName)
{
    if (fileName.isEmpty()) {
        return "";
    }

    QFile loadFile(getKeyMapPath() + "/" + fileName);
    if (!loadFile.open(QIODevice::ReadOnly)) {
        outLog("open file failed:" + fileName, true);
        return "";
    }

    QString ret = loadFile.readAll();
    loadFile.close();
    return ret;
}

void Dialog::slotActivated(QSystemTrayIcon::ActivationReason reason)
{
    switch (reason) {
    case QSystemTrayIcon::Trigger:
#ifdef Q_OS_WIN32
        this->show();
#endif
        break;
    default:
        break;
    }
}

void Dialog::closeEvent(QCloseEvent *event)
{
    this->hide();
    if (!Config::getInstance().getTrayMessageShown()) {
        Config::getInstance().setTrayMessageShown(true);
        m_hideIcon->showMessage(tr("Notice"),
                                tr("Hidden here!"),
                                QSystemTrayIcon::Information,
                                3000);
    }
    event->ignore();
}

void Dialog::on_updateDevice_clicked()
{
    if (checkAdbRun()) {
        return;
    }
    outLog("update devices...", false);
    m_adb.execute("", QStringList() << "devices");
}

void Dialog::on_startServerBtn_clicked()
{
    outLog("start server...", false);

    // this is ok that "original" toUshort is 0
    quint16 videoSize = ui->maxSizeBox->currentText().trimmed().toUShort();
    qsc::DeviceParams params;
    params.serial = ui->serialBox->currentText().trimmed();
    params.maxSize = videoSize;
    params.bitRate = getBitRate();
    // on devices with Android >= 10, the capture frame rate can be limited
    params.maxFps = static_cast<quint32>(Config::getInstance().getMaxFps());
    params.closeScreen = ui->closeScreenCheck->isChecked();
    params.useReverse = ui->useReverseCheck->isChecked();
    params.display = !ui->notDisplayCheck->isChecked();
    params.renderExpiredFrames = Config::getInstance().getRenderExpiredFrames();
    if (ui->lockOrientationBox->currentIndex() > 0) {
        params.captureOrientationLock = 1;
        params.captureOrientation = (ui->lockOrientationBox->currentIndex() - 1) * 90;
    }
    params.stayAwake = ui->stayAwakeCheck->isChecked();
    params.recordFile = ui->recordScreenCheck->isChecked();
    params.recordPath = ui->recordPathEdt->text().trimmed();
    params.recordFileFormat = ui->formatBox->currentText().trimmed();
    params.serverLocalPath = getServerPath();
    params.serverRemotePath = Config::getInstance().getServerPath();
    params.pushFilePath = Config::getInstance().getPushFilePath();
    params.gameScript = getGameScript(ui->gameBox->currentText());
    params.logLevel = Config::getInstance().getLogLevel();
    params.codecOptions = Config::getInstance().getCodecOptions();
    params.codecName = Config::getInstance().getCodecName();
    params.scid = QRandomGenerator::global()->bounded(1, 10000) & 0x7FFFFFFF;

    qsc::IDeviceManage::getInstance().connectDevice(params);
}

void Dialog::on_stopServerBtn_clicked()
{
    if (qsc::IDeviceManage::getInstance().disconnectDevice(ui->serialBox->currentText().trimmed())) {
        outLog("stop server");
    }
}

void Dialog::on_wirelessConnectBtn_clicked()
{
    if (checkAdbRun()) {
        return;
    }
    QString addr = ui->deviceIpEdt->currentText().trimmed();
    if (addr.isEmpty()) {
        outLog("error: device ip is null", false);
        return;
    }

    if (!ui->devicePortEdt->currentText().isEmpty()) {
        addr += ":";
        addr += ui->devicePortEdt->currentText().trimmed();
    } else if (!ui->devicePortEdt->lineEdit()->placeholderText().isEmpty()) {
        addr += ":";
        addr += ui->devicePortEdt->lineEdit()->placeholderText().trimmed();
    } else {
        outLog("error: device port is null", false);
        return;
    }

    // 保存IP历史记录 - 只保存IP部分,不包含端口
    QString ip = addr.split(":").first();
    if (!ip.isEmpty()) {
        saveIpHistory(ip);
    }
    
    // 保存端口历史记录
    QString port = addr.split(":").last();
    if (!port.isEmpty() && port != ip) {
        savePortHistory(port);
    }

    outLog("wireless connect...", false);
    QStringList adbArgs;
    adbArgs << "connect";
    adbArgs << addr;
    m_adb.execute("", adbArgs);
}

void Dialog::on_startAdbdBtn_clicked()
{
    if (checkAdbRun()) {
        return;
    }
    outLog("start devices adbd...", false);
    // adb tcpip 5555
    QStringList adbArgs;
    adbArgs << "tcpip";
    adbArgs << "5555";
    m_adb.execute(ui->serialBox->currentText().trimmed(), adbArgs);
}

void Dialog::outLog(const QString &log, bool newLine)
{
    // avoid sub thread update ui
    QString backLog = log;
    QTimer::singleShot(0, this, [this, backLog, newLine]() {
        ui->outEdit->append(backLog);
        if (newLine) {
            ui->outEdit->append("<br/>");
        }
    });
}

bool Dialog::filterLog(const QString &log)
{
    if (log.contains("app_proces")) {
        return true;
    }
    if (log.contains("Unable to set geometry")) {
        return true;
    }
    return false;
}

bool Dialog::checkAdbRun()
{
    if (m_adb.isRuning()) {
        outLog("wait for the end of the current command to run");
    }
    return m_adb.isRuning();
}

void Dialog::on_getIPBtn_clicked()
{
    if (checkAdbRun()) {
        return;
    }

    outLog("get ip...", false);
    // adb -s P7C0218510000537 shell ifconfig wlan0
    // or
    // adb -s P7C0218510000537 shell ip -f inet addr show wlan0
    QStringList adbArgs;
#if 0
    adbArgs << "shell";
    adbArgs << "ip";
    adbArgs << "-f";
    adbArgs << "inet";
    adbArgs << "addr";
    adbArgs << "show";
    adbArgs << "wlan0";
#else
    adbArgs << "shell";
    adbArgs << "ifconfig";
    adbArgs << "wlan0";
#endif
    m_adb.execute(ui->serialBox->currentText().trimmed(), adbArgs);
}

void Dialog::getIPbyIp()
{
    if (checkAdbRun()) {
        return;
    }

    QStringList adbArgs;
    adbArgs << "shell";
    adbArgs << "ip -o a";

    m_adb.execute(ui->serialBox->currentText().trimmed(), adbArgs);
}

void Dialog::onDeviceConnected(bool success, const QString &serial, const QString &deviceName, const QSize &size)
{
    Q_UNUSED(deviceName);
    if (!success) {
        return;
    }
    auto videoForm = new VideoForm(ui->framelessCheck->isChecked(), Config::getInstance().getSkin(), ui->showToolbar->isChecked());
    videoForm->setSerial(serial);

    qsc::IDeviceManage::getInstance().getDevice(serial)->setUserData(static_cast<void*>(videoForm));
    qsc::IDeviceManage::getInstance().getDevice(serial)->registerDeviceObserver(videoForm);


    videoForm->showFPS(ui->fpsCheck->isChecked());

    if (ui->alwaysTopCheck->isChecked()) {
        videoForm->staysOnTop();
    }

#ifndef Q_OS_WIN32
    // must be show before updateShowSize
    videoForm->show();
#endif
    QString name = Config::getInstance().getNickName(serial);
    if (name.isEmpty()) {
        name = Config::getInstance().getTitle();
    }
    videoForm->setWindowTitle(name + "-" + serial);
    videoForm->updateShowSize(size);

    bool deviceVer = size.height() > size.width();
    QRect rc = Config::getInstance().getRect(serial);
    bool rcVer = rc.height() > rc.width();
    // same width/height rate
    if (rc.isValid() && (deviceVer == rcVer)) {
        // mark: resize is for fix setGeometry magneticwidget bug
        videoForm->resize(rc.size());
        videoForm->setGeometry(rc);
    }

#ifdef Q_OS_WIN32
    // windows是show太早可以看到resize的过程
    QTimer::singleShot(200, videoForm, [videoForm](){videoForm->show();});
#endif

    GroupController::instance().addDevice(serial);
}

void Dialog::onDeviceDisconnected(QString serial)
{
    GroupController::instance().removeDevice(serial);
    auto device = qsc::IDeviceManage::getInstance().getDevice(serial);
    if (!device) {
        return;
    }
    auto data = device->getUserData();
    if (data) {
        VideoForm* vf = static_cast<VideoForm*>(data);
        qsc::IDeviceManage::getInstance().getDevice(serial)->deRegisterDeviceObserver(vf);
        vf->close();
        vf->deleteLater();
    }
}

void Dialog::on_wirelessDisConnectBtn_clicked()
{
    if (checkAdbRun()) {
        return;
    }
    QString addr = ui->deviceIpEdt->currentText().trimmed();
    outLog("wireless disconnect...", false);
    QStringList adbArgs;
    adbArgs << "disconnect";
    adbArgs << addr;
    m_adb.execute("", adbArgs);
}

void Dialog::on_selectRecordPathBtn_clicked()
{
    QFileDialog::Options options = QFileDialog::DontResolveSymlinks | QFileDialog::ShowDirsOnly;
    QString directory = QFileDialog::getExistingDirectory(this, tr("select path"), "", options);
    ui->recordPathEdt->setText(directory);
}

void Dialog::on_recordPathEdt_textChanged(const QString &arg1)
{
    ui->recordPathEdt->setToolTip(arg1.trimmed());
    ui->notDisplayCheck->setCheckable(!arg1.trimmed().isEmpty());
}

void Dialog::on_adbCommandBtn_clicked()
{
    execAdbCmd();
}

void Dialog::on_stopAdbBtn_clicked()
{
    m_adb.kill();
}

void Dialog::on_clearOut_clicked()
{
    ui->outEdit->clear();
}

void Dialog::on_stopAllServerBtn_clicked()
{
    qsc::IDeviceManage::getInstance().disconnectAllDevice();
}

void Dialog::on_refreshGameScriptBtn_clicked()
{
    ui->gameBox->clear();
    QDir dir(getKeyMapPath());
    if (!dir.exists()) {
        outLog("keymap directory not find", true);
        return;
    }
    dir.setFilter(QDir::Files | QDir::NoSymLinks);
    QFileInfoList list = dir.entryInfoList();
    QFileInfo fileInfo;
    int size = list.size();
    for (int i = 0; i < size; ++i) {
        fileInfo = list.at(i);
        ui->gameBox->addItem(fileInfo.fileName());
    }
}

void Dialog::on_applyScriptBtn_clicked()
{
    auto curSerial = ui->serialBox->currentText().trimmed();
    auto device = qsc::IDeviceManage::getInstance().getDevice(curSerial);
    if (!device) {
        return;
    }

    device->updateScript(getGameScript(ui->gameBox->currentText()));
}

void Dialog::on_recordScreenCheck_clicked(bool checked)
{
    if (!checked) {
        return;
    }

    QString fileDir(ui->recordPathEdt->text().trimmed());
    if (fileDir.isEmpty()) {
        qWarning() << "please select record save path!!!";
        ui->recordScreenCheck->setChecked(false);
    }
}

void Dialog::on_usbConnectBtn_clicked()
{
    on_stopAllServerBtn_clicked();
    delayMs(200);
    on_updateDevice_clicked();
    delayMs(200);

    int firstUsbDevice = findDeviceFromeSerialBox(false);
    if (-1 == firstUsbDevice) {
        qWarning() << "No use device is found!";
        return;
    }
    ui->serialBox->setCurrentIndex(firstUsbDevice);

    on_startServerBtn_clicked();
}

int Dialog::findDeviceFromeSerialBox(bool wifi)
{
    QString regStr = "\\b(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\:([0-9]|[1-9]\\d|[1-9]\\d{2}|[1-9]\\d{3}|[1-5]\\d{4}|6[0-4]\\d{3}|65[0-4]\\d{2}|655[0-2]\\d|6553[0-5])\\b";
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QRegExp regIP(regStr);
#else
    QRegularExpression regIP(regStr);
#endif
    for (int i = 0; i < ui->serialBox->count(); ++i) {
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        bool isWifi = regIP.exactMatch(ui->serialBox->itemText(i));
#else
        bool isWifi = regIP.match(ui->serialBox->itemText(i)).hasMatch();
#endif
        bool found = wifi ? isWifi : !isWifi;
        if (found) {
            return i;
        }
    }

    return -1;
}

void Dialog::on_wifiConnectBtn_clicked()
{
    on_stopAllServerBtn_clicked();
    delayMs(200);

    on_updateDevice_clicked();
    delayMs(200);

    int firstUsbDevice = findDeviceFromeSerialBox(false);
    if (-1 == firstUsbDevice) {
        qWarning() << "No use device is found!";
        return;
    }
    ui->serialBox->setCurrentIndex(firstUsbDevice);

    on_getIPBtn_clicked();
    delayMs(200);

    on_startAdbdBtn_clicked();
    delayMs(1000);

    on_wirelessConnectBtn_clicked();
    delayMs(2000);

    on_updateDevice_clicked();
    delayMs(200);

    int firstWifiDevice = findDeviceFromeSerialBox(true);
    if (-1 == firstWifiDevice) {
        qWarning() << "No wifi device is found!";
        return;
    }
    ui->serialBox->setCurrentIndex(firstWifiDevice);

    on_startServerBtn_clicked();
}

void Dialog::on_connectedPhoneList_itemDoubleClicked(QListWidgetItem *item)
{
    Q_UNUSED(item);
    ui->serialBox->setCurrentIndex(ui->connectedPhoneList->currentRow());
    on_startServerBtn_clicked();
}

void Dialog::on_updateNameBtn_clicked()
{
    if (ui->serialBox->count() != 0) {
        if (ui->userNameEdt->text().isEmpty()) {
            Config::getInstance().setNickName(ui->serialBox->currentText(), "Phone");
        } else {
            Config::getInstance().setNickName(ui->serialBox->currentText(), ui->userNameEdt->text());
        }

        on_updateDevice_clicked();

        qDebug() << "Update OK!";
    } else {
        qWarning() << "No device is connected!";
    }
}

void Dialog::on_useSingleModeCheck_clicked()
{
    if (ui->useSingleModeCheck->isChecked()) {
        ui->rightWidget->hide();
    } else {
        ui->rightWidget->show();
    }

    adjustSize();
}

void Dialog::on_serialBox_currentIndexChanged(const QString &arg1)
{
    ui->userNameEdt->setText(Config::getInstance().getNickName(arg1));
}

quint32 Dialog::getBitRate()
{
    return ui->bitRateEdit->text().trimmed().toUInt() *
            (ui->bitRateBox->currentText() == QString("Mbps") ? 1000000 : 1000);
}

const QString &Dialog::getServerPath()
{
    static QString serverPath;
    if (serverPath.isEmpty()) {
        serverPath = QString::fromLocal8Bit(qgetenv("QTSCRCPY_SERVER_PATH"));
        QFileInfo fileInfo(serverPath);
        if (serverPath.isEmpty() || !fileInfo.isFile()) {
            serverPath = QCoreApplication::applicationDirPath() + "/scrcpy-server";
        }
    }
    return serverPath;
}

void Dialog::on_startAudioBtn_clicked()
{
    if (ui->serialBox->count() == 0) {
        qWarning() << "No device is connected!";
        return;
    }

    m_audioOutput.start(ui->serialBox->currentText(), 28200);
}

void Dialog::on_stopAudioBtn_clicked()
{
    m_audioOutput.stop();
}

void Dialog::on_installSndcpyBtn_clicked()
{
    if (ui->serialBox->count() == 0) {
        qWarning() << "No device is connected!";
        return;
    }
    m_audioOutput.installonly(ui->serialBox->currentText(), 28200);
}

void Dialog::on_autoUpdatecheckBox_toggled(bool checked)
{
    if (checked) {
        m_autoUpdatetimer.start(5000);
    } else {
        m_autoUpdatetimer.stop();
    }
}

void Dialog::loadIpHistory()
{
    QStringList ipList = Config::getInstance().getIpHistory();
    ui->deviceIpEdt->clear();
    ui->deviceIpEdt->addItems(ipList);
    ui->deviceIpEdt->setContentsMargins(0, 0, 0, 0);

    if (ui->deviceIpEdt->lineEdit()) {
        ui->deviceIpEdt->lineEdit()->setMaxLength(128);
        ui->deviceIpEdt->lineEdit()->setPlaceholderText("192.168.0.1");
    }
}

void Dialog::saveIpHistory(const QString &ip)
{
    if (ip.isEmpty()) {
        return;
    }
    
    Config::getInstance().saveIpHistory(ip);
    
    // 更新ComboBox
    loadIpHistory();
    ui->deviceIpEdt->setCurrentText(ip);
}

void Dialog::showIpEditMenu(const QPoint &pos)
{
    QMenu *menu = ui->deviceIpEdt->lineEdit()->createStandardContextMenu();
    menu->addSeparator();
    
    QAction *clearHistoryAction = new QAction(tr("Clear History"), menu);
    connect(clearHistoryAction, &QAction::triggered, this, [this]() {
        Config::getInstance().clearIpHistory();
        loadIpHistory();
    });
    
    menu->addAction(clearHistoryAction);
    menu->exec(ui->deviceIpEdt->lineEdit()->mapToGlobal(pos));
    delete menu;
}

void Dialog::loadPortHistory()
{
    QStringList portList = Config::getInstance().getPortHistory();
    ui->devicePortEdt->clear();
    ui->devicePortEdt->addItems(portList);
    ui->devicePortEdt->setContentsMargins(0, 0, 0, 0);

    if (ui->devicePortEdt->lineEdit()) {
        ui->devicePortEdt->lineEdit()->setMaxLength(6);
        ui->devicePortEdt->lineEdit()->setPlaceholderText("5555");
    }
}

void Dialog::savePortHistory(const QString &port)
{
    if (port.isEmpty()) {
        return;
    }
    
    Config::getInstance().savePortHistory(port);
    
    // 更新ComboBox
    loadPortHistory();
    ui->devicePortEdt->setCurrentText(port);
}

void Dialog::showPortEditMenu(const QPoint &pos)
{
    QMenu *menu = ui->devicePortEdt->lineEdit()->createStandardContextMenu();
    menu->addSeparator();
    
    QAction *clearHistoryAction = new QAction(tr("Clear History"), menu);
    connect(clearHistoryAction, &QAction::triggered, this, [this]() {
        Config::getInstance().clearPortHistory();
        loadPortHistory();
    });
    
    menu->addAction(clearHistoryAction);
    menu->exec(ui->devicePortEdt->lineEdit()->mapToGlobal(pos));
    delete menu;
}

void Dialog::on_browseDeviceFileBtn_clicked()
{
    QString serial = ui->serialBox->currentText().trimmed();
    if (serial.isEmpty()) {
        QMessageBox::warning(this, "QtScrcpy", tr("Please select a device"), QMessageBox::Ok);
        return;
    }
    
    // 获取当前路径
    QString currentPath = ui->downloadPathEdt->text().trimmed();
    if (currentPath.isEmpty()) {
        currentPath = "/sdcard/";
    } else {
        // 提取目录部分
        if (currentPath.contains("/")) {
            int lastSlash = currentPath.lastIndexOf("/");
            if (lastSlash >= 0) {
                currentPath = currentPath.left(lastSlash + 1);
            }
        } else {
            currentPath = "/sdcard/";
        }
    }
    
    // 创建文件浏览器对话框（下载需选文件）
    DeviceFileBrowser browser(serial, currentPath, this, true);
    if (browser.exec() == QDialog::Accepted) {
        QString selectedPath = browser.getSelectedPath();
        if (!selectedPath.isEmpty()) {
            ui->downloadPathEdt->setText(selectedPath);
        }
    }
}

void Dialog::on_downloadBtn_clicked()
{
    // 如果当前正在下载，点击按钮执行停止
    if (m_isFileTransferInProgress && m_isCurrentTransferDownload) {
        if (m_fileTransferAdb && m_fileTransferAdb->isRuning()) {
            m_isDownloadCancelling = true;
            ui->downloadBtn->setEnabled(false);
            ui->downloadBtn->setText(tr("Stopping..."));
            ui->downloadProgressBar->setFormat(tr("Stopping..."));
            m_fileTransferAdb->kill();
        }
        return;
    }

    QString serial = ui->serialBox->currentText().trimmed();
    if (serial.isEmpty()) {
        QMessageBox::warning(this, "QtScrcpy", tr("Please select a device"), QMessageBox::Ok);
        return;
    }
    
    QString devicePath = ui->downloadPathEdt->text().trimmed();
    if (devicePath.isEmpty()) {
        QMessageBox::warning(this, "QtScrcpy", tr("Please select device file"), QMessageBox::Ok);
        return;
    }
    
    QString localPath = QFileDialog::getSaveFileName(this,
                                                     tr("Save File"),
                                                     Config::getInstance().getPushFilePath() + QFileInfo(devicePath).fileName(),
                                                     tr("All Files (*)"));
    if (localPath.isEmpty()) {
        return;
    }
    
    if (m_isFileTransferInProgress) {
        QMessageBox::warning(this, "QtScrcpy", tr("File transfer in progress, please wait"), QMessageBox::Ok);
        return;
    }
    
    // 使用独立的 AdbProcess 实例进行文件传输
    if (m_fileTransferAdb) {
        m_fileTransferAdb->deleteLater();
    }
    m_fileTransferAdb = new qsc::AdbProcess(this);
    connect(m_fileTransferAdb, &qsc::AdbProcess::fileTransferProgress, this, &Dialog::onFileTransferProgress);
    connect(m_fileTransferAdb, &qsc::AdbProcess::transferLog, this, [this](const QString &line) {
        outLog(line, true);
    });
    m_isFileTransferInProgress = true;
    m_isCurrentTransferDownload = true;
    m_isDownloadCancelling = false;
    
    // 连接信号监听传输开始
    connect(m_fileTransferAdb, &qsc::AdbProcess::adbProcessResult, this, [this](qsc::AdbProcess::ADB_EXEC_RESULT processResult) {
        if (processResult == qsc::AdbProcess::AER_SUCCESS_START) {
            // 传输开始，显示明确进度
            if (m_isCurrentTransferDownload) {
                ui->downloadProgressBar->setMinimum(0);
                ui->downloadProgressBar->setMaximum(100);
                ui->downloadProgressBar->setFormat(tr("Downloading %p%"));
                ui->downloadProgressBar->setValue(0);
            } else {
                ui->uploadProgressBar->setMinimum(0);
                ui->uploadProgressBar->setMaximum(100);
                ui->uploadProgressBar->setFormat(tr("Uploading %p%"));
                ui->uploadProgressBar->setValue(0);
            }
            return;
        }
        
        bool success = (processResult == qsc::AdbProcess::AER_SUCCESS_EXEC);
        onFileTransferFinished(m_isCurrentTransferDownload, success);
        m_isFileTransferInProgress = false;
        if (m_fileTransferAdb) {
            m_fileTransferAdb->deleteLater();
            m_fileTransferAdb = nullptr;
        }
    });
    
    // 重置进度条并显示明确进度
    ui->downloadProgressBar->setMinimum(0);
    ui->downloadProgressBar->setMaximum(100);
    ui->downloadProgressBar->setValue(0);
    ui->downloadProgressBar->setFormat("%p%");
    ui->downloadProgressBar->setTextVisible(true);
    ui->downloadProgressBar->show();
    ui->downloadBtn->setEnabled(true);
    ui->downloadBtn->setText(tr("Stop"));
    
    // 开始下载
    m_fileTransferAdb->pull(serial, devicePath, localPath);
    outLog(tr("Starting file download..."), false);
}

void Dialog::on_browseLocalFolderBtn_clicked()
{
    QFileDialog dialog(this,
                       tr("Select File or Folder to Upload"),
                       Config::getInstance().getPushFilePath());
    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setOption(QFileDialog::ShowDirsOnly, false);

    // 允许在列表视图/树视图中选择目录
    if (QListView *listView = dialog.findChild<QListView*>("listView")) {
        listView->setSelectionMode(QAbstractItemView::SingleSelection);
    }
    if (QTreeView *treeView = dialog.findChild<QTreeView*>("treeView")) {
        treeView->setSelectionMode(QAbstractItemView::SingleSelection);
    }

    if (dialog.exec() == QDialog::Accepted) {
        const QStringList selected = dialog.selectedFiles();
        if (!selected.isEmpty()) {
            QString path = selected.first();
            ui->uploadDevicePathEdt->setText(path);
            m_selectedUploadFile = path; // 存储选择的文件或文件夹路径
        }
    }
}

void Dialog::on_browseDeviceFolderBtn_clicked()
{
    QString serial = ui->serialBox->currentText().trimmed();
    if (serial.isEmpty()) {
        QMessageBox::warning(this, "QtScrcpy", tr("Please select a device"), QMessageBox::Ok);
        return;
    }

    QString currentPath = ui->uploadTargetPathEdt->text().trimmed();
    if (currentPath.isEmpty()) {
        currentPath = "/sdcard/";
    }

    // 上传目标仅允许选择目录
    DeviceFileBrowser browser(serial, currentPath, this, false);
    if (browser.exec() == QDialog::Accepted) {
        QString selectedPath = browser.getSelectedPath();
        if (!selectedPath.isEmpty()) {
            // 确保目录路径以 / 结尾
            if (!selectedPath.endsWith("/")) {
                selectedPath += "/";
            }
            ui->uploadTargetPathEdt->setText(selectedPath);
        }
    }
}

void Dialog::on_uploadBtn_clicked()
{
    QString serial = ui->serialBox->currentText().trimmed();
    if (serial.isEmpty()) {
        QMessageBox::warning(this, "QtScrcpy", tr("Please select a device"), QMessageBox::Ok);
        return;
    }
    
    QString localPath = ui->uploadDevicePathEdt->text().trimmed();
    if (localPath.isEmpty()) {
        QMessageBox::warning(this, "QtScrcpy", tr("Please select a file or folder to upload"), QMessageBox::Ok);
        return;
    }
    
    QFileInfo localInfo(localPath);
    if (!localInfo.exists()) {
        QMessageBox::warning(this, "QtScrcpy", tr("Selected path does not exist"), QMessageBox::Ok);
        return;
    }
    
    if (m_isFileTransferInProgress) {
        QMessageBox::warning(this, "QtScrcpy", tr("File transfer in progress, please wait"), QMessageBox::Ok);
        return;
    }
    
    QString devicePath = ui->uploadTargetPathEdt->text().trimmed();
    if (devicePath.isEmpty()) {
        QMessageBox::warning(this, "QtScrcpy", tr("Please select device folder"), QMessageBox::Ok);
        return;
    }
    if (!devicePath.endsWith("/")) {
        devicePath += "/";
    }
    
    // 使用独立的 AdbProcess 实例进行文件传输
    if (m_fileTransferAdb) {
        m_fileTransferAdb->deleteLater();
    }
    m_fileTransferAdb = new qsc::AdbProcess(this);
    connect(m_fileTransferAdb, &qsc::AdbProcess::fileTransferProgress, this, &Dialog::onFileTransferProgress);
    connect(m_fileTransferAdb, &qsc::AdbProcess::transferLog, this, [this](const QString &line) {
        outLog(line, true);
    });
    m_isFileTransferInProgress = true;
    m_isCurrentTransferDownload = false;
    
    // 连接信号监听传输开始和完成
    connect(m_fileTransferAdb, &qsc::AdbProcess::adbProcessResult, this, [this](qsc::AdbProcess::ADB_EXEC_RESULT processResult) {
        if (processResult == qsc::AdbProcess::AER_SUCCESS_START) {
            // 传输开始，显示明确进度
            if (m_isCurrentTransferDownload) {
                ui->downloadProgressBar->setMinimum(0);
                ui->downloadProgressBar->setMaximum(100);
                ui->downloadProgressBar->setFormat(tr("Downloading %p%"));
                ui->downloadProgressBar->setValue(0);
            } else {
                ui->uploadProgressBar->setMinimum(0);
                ui->uploadProgressBar->setMaximum(100);
                ui->uploadProgressBar->setFormat(tr("Uploading %p%"));
                ui->uploadProgressBar->setValue(0);
            }
            return;
        }
        
        bool success = (processResult == qsc::AdbProcess::AER_SUCCESS_EXEC);
        onFileTransferFinished(m_isCurrentTransferDownload, success);
        m_isFileTransferInProgress = false;
        if (m_fileTransferAdb) {
            m_fileTransferAdb->deleteLater();
            m_fileTransferAdb = nullptr;
        }
    });
    
    // 重置进度条并显示明确进度
    ui->uploadProgressBar->setMinimum(0);
    ui->uploadProgressBar->setMaximum(100);
    ui->uploadProgressBar->setValue(0);
    ui->uploadProgressBar->setFormat("%p%");
    ui->uploadProgressBar->setTextVisible(true);
    ui->uploadProgressBar->show();
    ui->uploadBtn->setEnabled(false);
    
    // 开始上传
    // 如果是文件夹，ADB push 会递归上传整个文件夹
    m_fileTransferAdb->push(serial, localPath, devicePath);
    outLog(tr("Starting file upload..."), false);
}

void Dialog::onFileTransferProgress(bool isDownload, int progress)
{
    if (isDownload) {
        if (progress < 0) {
            // 不确定状态 - 显示动画
            ui->downloadProgressBar->setMinimum(0);
            ui->downloadProgressBar->setMaximum(0);
            ui->downloadProgressBar->setValue(0);
        } else {
            ui->downloadProgressBar->setMinimum(0);
            ui->downloadProgressBar->setMaximum(100);
            ui->downloadProgressBar->setValue(progress);
            ui->downloadProgressBar->setFormat("%p%");
        }
    } else {
        if (progress < 0) {
            // 不确定状态 - 显示动画
            ui->uploadProgressBar->setMinimum(0);
            ui->uploadProgressBar->setMaximum(0);
            ui->uploadProgressBar->setValue(0);
        } else {
            ui->uploadProgressBar->setMinimum(0);
            ui->uploadProgressBar->setMaximum(100);
            ui->uploadProgressBar->setValue(progress);
            ui->uploadProgressBar->setFormat("%p%");
        }
    }
}

void Dialog::onFileTransferFinished(bool isDownload, bool success)
{
    if (isDownload) {
        ui->downloadProgressBar->setMinimum(0);
        ui->downloadProgressBar->setMaximum(100);
        ui->downloadProgressBar->setValue(success ? 100 : 0);
        if (m_isDownloadCancelling) {
            ui->downloadProgressBar->setFormat(tr("Download canceled"));
            outLog(tr("File download canceled"), false);
        } else {
            if (success) {
                ui->downloadProgressBar->setFormat(tr("Download completed"));
            } else {
                ui->downloadProgressBar->setFormat(tr("Download failed"));
            }
            if (success) {
                outLog(tr("File download completed successfully"), false);
            } else {
                outLog(tr("File download failed"), false);
            }
        }
        ui->downloadBtn->setEnabled(true);
        ui->downloadBtn->setText(tr("Download"));
        // 重置进度条显示
        QTimer::singleShot(3000, [this]() {
            ui->downloadProgressBar->setValue(0);
            ui->downloadProgressBar->setFormat("%p%");
        });
        m_isDownloadCancelling = false;
    } else {
        ui->uploadProgressBar->setMinimum(0);
        ui->uploadProgressBar->setMaximum(100);
        ui->uploadProgressBar->setValue(success ? 100 : 0);
        if (success) {
            ui->uploadProgressBar->setFormat(tr("Upload completed"));
        } else {
            ui->uploadProgressBar->setFormat(tr("Upload failed"));
        }
        ui->uploadBtn->setEnabled(true);
        if (success) {
            outLog(tr("File upload completed successfully"), false);
            // 重置进度条
            QTimer::singleShot(3000, [this]() {
                ui->uploadProgressBar->setValue(0);
                ui->uploadProgressBar->setFormat("%p%");
            });
        } else {
            outLog(tr("File upload failed"), false);
            // 保持失败状态显示一段时间
            QTimer::singleShot(3000, [this]() {
                ui->uploadProgressBar->setValue(0);
                ui->uploadProgressBar->setFormat("%p%");
            });
        }
    }
}
