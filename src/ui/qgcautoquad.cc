#include "MainWindow.h"
#include "qgcautoquad.h"
#include "ui_qgcautoquad.h"
#include "LinkManager.h"
#include "UASManager.h"
#include "MG.h"
#include <SerialLinkInterface.h>
#include <SerialLink.h>
#include "../configuration.h"
#include "GAudioOutput.h"

#include <QWidget>
#include <QFileDialog>
#include <QTextBrowser>
#include <QMessageBox>
#include <QSignalMapper>
#include <QStringList>

#include "qextserialenumerator.h"

QGCAutoquad::QGCAutoquad(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::QGCAutoquad),
    uas(NULL),
    paramaq(NULL),
    esc32(NULL),
    connectedLink(NULL),
    mtx_paramsAreLoading(false)
{

    VisibleWidget = 0;
    FwFileForEsc32 = "";
    FlashEsc32Active = false;
    fwFlashActive = false;

    aqFirmwareVersion = "";
    aqFirmwareRevision = 0;
    aqHardwareVersion = 6;
    aqHardwareRevision = 0;
    aqBuildNumber = 0;

    aqBinFolderPath = QCoreApplication::applicationDirPath() + "/aq/bin/";
    aqMotorMixesPath = QCoreApplication::applicationDirPath() + "/aq/mixes/";
#if defined(Q_OS_WIN)
    platformExeExt = ".exe";
#else
    platformExeExt = "";
#endif

    // these regexes are used for matching field names to AQ params
    fldnameRx.setPattern("^(COMM|CTRL|DOWNLINK|GMBL|GPS|IMU|L1|MOT|NAV|PPM|RADIO|SIG|SPVR|UKF|VN100|QUATOS|LIC)_[A-Z0-9_]+$"); // strict field name matching
    dupeFldnameRx.setPattern("___N[0-9]"); // for having duplicate field names, append ___N# after the field name (three underscores, "N", and a unique number)

    setHardwareInfo(aqHardwareVersion);  // populate hardware (AQ board) info with defaults
    setFirmwareInfo();  // set defaults based on fw version

    /*
     * Start the UI
    */

    ui->setupUi(this);

    // load the port config UI
    aqPwmPortConfig = new AQPWMPortsConfig(this);
    ui->tabLayout_aqMixingOutput->addWidget(aqPwmPortConfig);
    //ui->tab_aq_settings->insertTab(2, aqPwmPortConfig, tr("Mixing && Output"));

    // set up the splitter expand/collapse button
    ui->splitter->setStyleSheet("QSplitter#splitter {width: 15px;}");
    QSplitterHandle *shandle = ui->splitter->handle(1);
    shandle->setContentsMargins(0, 15, 0, 0);
    shandle->setToolTip(tr("<html><body><p>Click the arrow button to collapse/expand the left sidebar. Click and drag anywhere to resize.</p></body></html>"));
    QVBoxLayout *hlayout = new QVBoxLayout;
    hlayout->setContentsMargins(0, 0, 0, 0);
    splitterToggleBtn = new QToolButton(shandle);
    splitterToggleBtn->setObjectName("toolButton_splitterToggleBtn");
    splitterToggleBtn->setArrowType(Qt::LeftArrow);
    splitterToggleBtn->setCursor(QCursor(Qt::ArrowCursor));
    hlayout->addWidget(splitterToggleBtn);
    hlayout->setAlignment(splitterToggleBtn, Qt::AlignTop);
    hlayout->addStretch(3);
    shandle->setLayout(hlayout);

    // populate field values

    ui->checkBox_raw_value->setChecked(true);

    // multiple-radio mode selector
    ui->comboBox_multiRadioMode->addItem("Diversity", 0);
    ui->comboBox_multiRadioMode->addItem("Split", 1);

    ui->SPVR_FS_RAD_ST1->addItem("Position Hold", 0);

//    ui->CTRL_HF_ON_POS->addItem("High", 250);
//    ui->CTRL_HF_ON_POS->addItem("Mid", 0);
//    ui->CTRL_HF_ON_POS->addItem("Low", -250);
//    ui->CTRL_HF_ON_POS->setCurrentIndex(2);

    ui->STARTUP_MODE->addItem("Open Loop",0);
    ui->STARTUP_MODE->addItem("CL RPM",1);
    ui->STARTUP_MODE->addItem("CL Thrust",2);
    ui->STARTUP_MODE->addItem("Servo (v1.5+)",3);

//    ui->comboBox_in_mode->addItem("PWM",0);
//    ui->comboBox_in_mode->addItem("UART",1);
//    ui->comboBox_in_mode->addItem("I2C",2);
//    ui->comboBox_in_mode->addItem("CAN",3);
//    ui->comboBox_in_mode->addItem("OW", 4);

    // baud rates

    QList<int> availableBaudRates = MG::SERIAL::getBaudRates();
    QStringList baudRates;
    for (int i=0; i < availableBaudRates.length(); ++i)
        baudRates.append(QString::number(availableBaudRates[i]));

    ui->DOWNLINK_BAUD->addItems(baudRates);
    ui->COMM_BAUD1->addItems(baudRates);
    ui->COMM_BAUD2->addItems(baudRates);
    ui->COMM_BAUD3->addItems(baudRates);
    ui->COMM_BAUD4->addItems(baudRates);
    ui->BAUD_RATE->addItems(baudRates);

    ui->comboBox_esc32PortSpeed->addItems(baudRates);

    // firmware types
    ui->comboBox_fwType->addItem(tr("AutoQuad Serial"), "aq");
    ui->comboBox_fwType->addItem(tr("AutoQuad Native USB"), "dfu");
    ui->comboBox_fwType->addItem(tr("ESC32 Serial"), "esc32");
    ui->comboBox_fwType->setCurrentIndex(0);

    // firmware serial flash baud rates
    QStringList flashBaudRates;
    flashBaudRates << "38400" <<  "57600" << "115200";
    ui->comboBox_fwPortSpeed->addItems(flashBaudRates);


    // populate COMM stream types
    QList<QButtonGroup *> commStreamTypBtns = this->findChildren<QButtonGroup *>(QRegExp("COMM_STREAM_TYP[\\d]$"));
    foreach (QButtonGroup* g, commStreamTypBtns) {
        foreach (QAbstractButton* abtn, g->buttons()) {
            QString ctyp = abtn->objectName().replace(QRegExp("[\\w]+_[\\w]+_"), "");
            if (ctyp == "multiplex")
                g->setId(abtn, COMM_TYPE_MULTIPLEX);
            else if (ctyp == "mavlink")
                g->setId(abtn, COMM_TYPE_MAVLINK);
            else if (ctyp == "telemetry")
                g->setId(abtn, COMM_TYPE_TELEMETRY);
            else if (ctyp == "gps")
                g->setId(abtn, COMM_TYPE_GPS);
            else if (ctyp == "file")
                g->setId(abtn, COMM_TYPE_FILEIO);
            else if (ctyp == "cli")
                g->setId(abtn, COMM_TYPE_CLI);
            else if (ctyp == "omapConsole")
                g->setId(abtn, COMM_TYPE_OMAP_CONSOLE);
            else if (ctyp == "omapPpp")
                g->setId(abtn, COMM_TYPE_OMAP_PPP);
            else
                g->setId(abtn, COMM_TYPE_NONE);
        }
    }

    // Final UI tweaks

    ui->label_radioChangeWarning->hide();
    ui->groupBox_ppmOptions->hide();
    ui->groupBox_ppmOptions->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    ui->conatiner_radioGraphValues->setEnabled(false);

    // hide some controls which may get shown later based on AQ fw version
    ui->comboBox_multiRadioMode->hide();
    ui->label_multiRadioMode->hide();
    ui->DOWNLINK_BAUD->hide();
    ui->label_DOWNLINK_BAUD->hide();
    ui->MOT_MIN->hide();
    ui->label_MOT_MIN->hide();
    ui->CTRL_MAN_YAW_RT->hide();
    ui->label_CTRL_MAN_YAW_RT->hide();
    ui->cmdBtn_ConvertTov68AttPIDs->hide();

    // hide these permanently, for now... (possible future use for these)
    ui->checkBox_raw_value->hide();
    ui->label_portName_esc32->hide();
    ui->pushButton_logging->hide();

    ui->widget_controlAdvancedSettings->setVisible(ui->groupBox_controlAdvancedSettings->isChecked());
//    ui->widget_ppmOptions->setVisible(ui->groupBox_ppmOptions->isChecked());

    adjustUiForHardware();
    adjustUiForFirmware();

    ui->pushButton_start_calibration->setToolTip("WARNING: EXPERIMENTAL!!");

#ifdef QT_NO_DEBUG
    ui->tab_aq_settings->removeTab(ui->tab_aq_settings->count()-1); // hide devel tab
#endif


    // done setting up UI //

    delayedSendRCTimer.setInterval(800);  // timer for sending radio freq. update value

    // save this for easy iteration later
    allRadioChanCombos.append(ui->groupBox_channelMapping->findChildren<QComboBox *>(QRegExp("^(RADIO_|NAV_|GMBL_).+_CH")));
    allRadioChanProgressBars.append(ui->groupBox_Radio_Values->findChildren<QProgressBar *>(QRegExp("progressBar_chan_[0-9]")));
    allRadioChanValueLabels.append(ui->groupBox_Radio_Values->findChildren<QLabel *>(QRegExp("label_chanValue_[0-9]")));


    // Signal handlers

    connect(this, SIGNAL(hardwareInfoUpdated()), this, SLOT(adjustUiForHardware()));
    connect(this, SIGNAL(firmwareInfoUpdated()), this, SLOT(adjustUiForFirmware()));

    //
    // GUI slots

    // splitter
    connect(splitterToggleBtn, SIGNAL(clicked()), this, SLOT(splitterCollapseToggle()));
    connect(ui->splitter, SIGNAL(splitterMoved(int,int)), this, SLOT(splitterMoved()));

    connect(ui->RADIO_TYPE, SIGNAL(currentIndexChanged(int)), this, SLOT(radioType_changed(int)));
    connect(ui->RADIO_SETUP, SIGNAL(currentIndexChanged(int)), this, SLOT(radioType_changed(int)));
    connect(ui->comboBox_radioSetup2, SIGNAL(currentIndexChanged(int)), this, SLOT(radioType_changed(int)));
    connect(ui->SelectFirmwareButton, SIGNAL(clicked()), this, SLOT(selectFWToFlash()));
    connect(ui->portName, SIGNAL(currentIndexChanged(QString)), this, SLOT(setPortName(QString)));
    connect(ui->comboBox_fwPortSpeed, SIGNAL(currentIndexChanged(QString)), this, SLOT(setPortName(QString)));
    connect(ui->flashButton, SIGNAL(clicked()), this, SLOT(flashFW()));
    connect(ui->comboBox_fwType, SIGNAL(currentIndexChanged(int)), this, SLOT(fwTypeChange()));
    connect(ui->toolButton_fwReloadPorts, SIGNAL(clicked()), this, SLOT(setupPortList()));
    connect(ui->toolButton_esc32ReloadPorts, SIGNAL(clicked()), this, SLOT(setupPortList()));
    //connect(LinkManager::instance(), SIGNAL(newLink(LinkInterface*)), this, SLOT(addLink(LinkInterface*)));

//    connect(ui->comboBox_port_esc32, SIGNAL(editTextChanged(QString)), this, SLOT(setPortNameEsc32(QString)));
    connect(ui->comboBox_port_esc32, SIGNAL(currentIndexChanged(QString)), this, SLOT(setPortNameEsc32(QString)));
    connect(ui->comboBox_esc32PortSpeed, SIGNAL(currentIndexChanged(QString)), this, SLOT(setPortNameEsc32(QString)));
    connect(ui->pushButton_connect_to_esc32, SIGNAL(clicked()), this, SLOT(btnConnectEsc32()));
    connect(ui->pushButton_read_config, SIGNAL(clicked()), this, SLOT(btnReadConfigEsc32()));
    connect(ui->pushButton_send_to_esc32, SIGNAL(clicked()), this, SLOT(btnSaveToEsc32()));
    connect(ui->pushButton_esc32_read_arm_disarm, SIGNAL(clicked()), this, SLOT(btnArmEsc32()));
    connect(ui->pushButton_esc32_read_start_stop, SIGNAL(clicked()), this, SLOT(btnStartStopEsc32()));
    connect(ui->pushButton_send_rpm, SIGNAL(clicked()), this, SLOT(btnSetRPM()));
    connect(ui->pushButton_start_calibration, SIGNAL(clicked()), this, SLOT(Esc32StartCalibration()));
    connect(ui->pushButton_logging, SIGNAL(clicked()), this, SLOT(Esc32StartLogging()));
    connect(ui->pushButton_read_load_def, SIGNAL(clicked()), this, SLOT(Esc32LoadDefaultConf()));
    connect(ui->pushButton_reload_conf, SIGNAL(clicked()), this, SLOT(Esc32ReLoadConf()));
    connect(ui->pushButton_esc32_saveToFile, SIGNAL(clicked()), this, SLOT(Esc32SaveParamsToFile()));
    connect(ui->pushButton_esc32_loadFromFile, SIGNAL(clicked()), this, SLOT(Esc32LoadParamsFromFile()));

    connect(ui->pushButton_save_to_aq, SIGNAL(clicked()),this,SLOT(saveAQSettings()));
    connect(ui->cmdBtn_ConvertTov68AttPIDs, SIGNAL(clicked()), this, SLOT(convertPidAttValsToFW68Scales()));

    connect(&delayedSendRCTimer, SIGNAL(timeout()), this, SLOT(sendRcRefreshFreq()));
    connect(ui->checkBox_raw_value, SIGNAL(clicked()),this,SLOT(toggleRadioValuesUpdate()));
    connect(ui->pushButton_toggleRadioGraph, SIGNAL(clicked()),this,SLOT(toggleRadioValuesUpdate()));
    connect(ui->spinBox_rcGraphRefreshFreq, SIGNAL(valueChanged(int)), this, SLOT(delayedSendRcRefreshFreq(int)));
    foreach (QComboBox* cb, allRadioChanCombos)
        connect(cb, SIGNAL(currentIndexChanged(int)), this, SLOT(validateRadioSettings(int)));

#ifndef QT_NO_DEBUG
    connect(ui->pushButton_dev1, SIGNAL(clicked()),this, SLOT(pushButton_dev1()));
    connect(ui->pushButton_ObjectTracking, SIGNAL(clicked()),this, SLOT(pushButton_tracking()));
    connect(ui->pushButton_ObjectTracking_File, SIGNAL(clicked()),this, SLOT(pushButton_tracking_file()));
#endif

    //Process Slots
    ps_master.setProcessChannelMode(QProcess::MergedChannels);
    connect(&ps_master, SIGNAL(finished(int)), this, SLOT(prtstexit(int)));
    connect(&ps_master, SIGNAL(readyReadStandardOutput()), this, SLOT(prtstdout()));
//    connect(&ps_master, SIGNAL(readyReadStandardError()), this, SLOT(prtstderr()));
    connect(&ps_master, SIGNAL(error(QProcess::ProcessError)), this, SLOT(extProcessError(QProcess::ProcessError)));

    //Process Slots for tracking
    ps_tracking.setProcessChannelMode(QProcess::MergedChannels);
    connect(&ps_tracking, SIGNAL(finished(int)), this, SLOT(prtstexitTR(int)));
    connect(&ps_tracking, SIGNAL(readyReadStandardOutput()), this, SLOT(prtstdoutTR()));
    connect(&ps_tracking, SIGNAL(readyReadStandardError()), this, SLOT(prtstderrTR()));
    TrackingIsrunning = 0;

    setupPortList();
    loadSettings();

    // UAS slots
    QList<UASInterface*> mavs = UASManager::instance()->getUASList();
    foreach (UASInterface* currMav, mavs) {
        addUAS(currMav);
    }
    setActiveUAS(UASManager::instance()->getActiveUAS());
    connect(UASManager::instance(), SIGNAL(UASCreated(UASInterface*)), this, SLOT(addUAS(UASInterface*)), Qt::UniqueConnection);
    connect(UASManager::instance(), SIGNAL(activeUASSet(UASInterface*)), this, SLOT(setActiveUAS(UASInterface*)), Qt::UniqueConnection);

}

QGCAutoquad::~QGCAutoquad()
{
    if (esc32)
        btnConnectEsc32();
    if (ps_master.state() == QProcess::Running)
        ps_master.close();
    if (ps_tracking.state() == QProcess::Running)
        ps_tracking.close();

    writeSettings();
    delete ui;
}

void QGCAutoquad::changeEvent(QEvent *e)
{
    QWidget::changeEvent(e);
    switch (e->type()) {
    case QEvent::LanguageChange:
        ui->retranslateUi(this);
        break;
    default:
        break;
    }
}

void QGCAutoquad::hideEvent(QHideEvent* event)
{
    if ( VisibleWidget <= 1)
        VisibleWidget = 0;
    QWidget::hideEvent(event);
    emit visibilityChanged(false);
}

void QGCAutoquad::showEvent(QShowEvent* event)
{
    // React only to internal (pre-display)
    // events
    if ( VisibleWidget <= 1)
        VisibleWidget = 1;

    if ( VisibleWidget == 1) {
        if ( uas != NULL)
        {
            setActiveUAS(uas);
            VisibleWidget = 2;
        }
    }
    QWidget::showEvent(event);
    emit visibilityChanged(true);
}

void QGCAutoquad::loadSettings()
{
    // Load defaults from settings
    // QSettings settings("Aq.ini", QSettings::IniFormat);

    settings.beginGroup("AUTOQUAD_SETTINGS");

    // if old style Aq.ini file exists, copy settings to QGC shared storage
    if (QFile("Aq.ini").exists()) {
        QSettings aq_settings("Aq.ini", QSettings::IniFormat);
        aq_settings.beginGroup("AUTOQUAD_SETTINGS");
        foreach (QString childKey, aq_settings.childKeys())
            settings.setValue(childKey, aq_settings.value(childKey));
        settings.sync();
        QFile("Aq.ini").rename("Aq.ini.bak");
        qDebug() << "Copied settings from Aq.ini to QGC shared config storage.";
    }

    ui->portName->setCurrentIndex(ui->portName->findText(settings.value("FW_FLASH_PORT_NAME", "").toString()));
    ui->comboBox_fwPortSpeed->setCurrentIndex(ui->comboBox_fwPortSpeed->findText(settings.value("FW_FLASH_BAUD_RATE", 115200).toString()));
    ui->comboBox_port_esc32->setCurrentIndex(ui->comboBox_port_esc32->findText(settings.value("ESC32_FLASH_PORT_NAME", "").toString()));
    ui->comboBox_esc32PortSpeed->setCurrentIndex(ui->comboBox_esc32PortSpeed->findText(settings.value("ESC32_BAUD_RATE", 230400).toString()));

    if (settings.contains("AUTOQUAD_FW_FILE") && settings.value("AUTOQUAD_FW_FILE").toString().length()) {
        ui->fileLabel->setText(settings.value("AUTOQUAD_FW_FILE").toString());
        ui->fileLabel->setToolTip(settings.value("AUTOQUAD_FW_FILE").toString());
        ui->checkBox_verifyFwFlash->setChecked(settings.value("AUTOQUAD_FW_VERIFY", true).toBool());
        setFwType();
    }

    LastFilePath = settings.value("AUTOQUAD_LAST_PATH").toString();

    if (settings.contains("SETTINGS_SPLITTER_SIZES")) {
        ui->splitter->restoreState(settings.value("SETTINGS_SPLITTER_SIZES").toByteArray());
        splitterMoved();
    }

    ui->tabWidget_aq_left->setCurrentIndex(settings.value("SETTING_SELECTED_LEFT_TAB", 0).toInt());
    ui->pushButton_toggleRadioGraph->setChecked(settings.value("RADIO_VALUES_UPDATE_BTN_STATE", true).toBool());
    ui->groupBox_controlAdvancedSettings->setChecked(settings.value("ADDL_CTRL_SETTINGS_GRP_STATE", ui->groupBox_controlAdvancedSettings->isChecked()).toBool());

    settings.endGroup();
    settings.sync();
}

void QGCAutoquad::writeSettings()
{
    //QSettings settings("Aq.ini", QSettings::IniFormat);
    settings.beginGroup("AUTOQUAD_SETTINGS");

    settings.setValue("APP_VERSION", QGCAUTOQUAD::APP_VERSION);

    settings.setValue("AUTOQUAD_FW_FILE", ui->fileLabel->text());
    settings.setValue("AUTOQUAD_FW_VERIFY", ui->checkBox_verifyFwFlash->isChecked());
    settings.setValue("FW_FLASH_PORT_NAME", ui->portName->currentText());
    settings.setValue("FW_FLASH_BAUD_RATE", ui->comboBox_fwPortSpeed->currentText());
    settings.setValue("ESC32_FLASH_PORT_NAME", ui->comboBox_port_esc32->currentText());
    settings.setValue("ESC32_BAUD_RATE", ui->comboBox_esc32PortSpeed->currentText());

    settings.setValue("AUTOQUAD_LAST_PATH", LastFilePath);

    settings.setValue("SETTINGS_SPLITTER_SIZES", ui->splitter->saveState());
    settings.setValue("SETTING_SELECTED_LEFT_TAB", ui->tabWidget_aq_left->currentIndex());
    settings.setValue("RADIO_VALUES_UPDATE_BTN_STATE", ui->pushButton_toggleRadioGraph->isChecked());
    settings.setValue("ADDL_CTRL_SETTINGS_GRP_STATE", ui->groupBox_controlAdvancedSettings->isChecked());

    settings.sync();
    settings.endGroup();
}


//
// UI handlers
//

void QGCAutoquad::adjustUiForHardware() {
    ui->groupBox_commSerial2->setVisible(aqHardwareVersion == 6);
    ui->groupBox_commSerial3->setVisible(aqHardwareVersion == 7);
    ui->groupBox_commSerial4->setVisible(aqHardwareVersion == 7);
}

void QGCAutoquad::adjustUiForFirmware() {

    // which radio types are available
    QStringList radioTypes;
    radioTypes << "No Radio" << "Spektrum 11Bit" << "Spektrum 10Bit" << "S-BUS (Futaba, others)" << "PPM";
    if (!aqBuildNumber || aqBuildNumber >= 1149)
        radioTypes << "SUMD (Graupner)";
    if (!aqBuildNumber || aqBuildNumber >= 1350)
        radioTypes << "M-Link (Multiplex)";
    if (!aqBuildNumber || aqBuildNumber >= 1739)
        radioTypes << "Deltang";

    setupRadioTypes(radioTypes);

    ui->RADIO_TYPE->setVisible(!useRadioSetupParam);
    ui->label_RADIO_TYPE->setVisible(!useRadioSetupParam);
    ui->comboBox_radioSetup2->setVisible(useRadioSetupParam);
    ui->label_radioSetup2->setVisible(useRadioSetupParam);
//    ui->comboBox_multiRadioMode->setVisible(useRadioSetupParam);
//    ui->label_multiRadioMode->setVisible(useRadioSetupParam);

    // radio loss stage 2 failsafe options
    uint8_t idx = ui->SPVR_FS_RAD_ST2->currentIndex();
    ui->SPVR_FS_RAD_ST2->clear();
    ui->SPVR_FS_RAD_ST2->addItem("Land", 0);
    ui->SPVR_FS_RAD_ST2->addItem("RTH, Land", 1);
    if (!aqBuildNumber || aqBuildNumber >= 1304)
        ui->SPVR_FS_RAD_ST2->addItem("Ascend, RTH, Land", 2);
    if (idx < ui->SPVR_FS_RAD_ST2->count())
        ui->SPVR_FS_RAD_ST2->setCurrentIndex(idx);

    // gimbal auto-triggering options
    ui->groupBox_gmbl_auto_triggering->setVisible(!aqBuildNumber || aqBuildNumber >= 1378);

    // param widget buttons
    if (paramaq) {
        paramaq->setRestartBtnEnabled(aqCanReboot);
        paramaq->setCalibBtnsEnabled(!aqBuildNumber || aqBuildNumber >= 1760);
    }
}

void QGCAutoquad::setupRadioTypes(const QStringList &radioTypes)
{
    uint8_t idx = ui->RADIO_TYPE->currentIndex(),
            idx2 = ui->RADIO_SETUP->currentIndex(),
            idx3 = ui->comboBox_radioSetup2->currentIndex();

    ui->RADIO_TYPE->blockSignals(true);
    ui->RADIO_SETUP->blockSignals(true);
    ui->comboBox_radioSetup2->blockSignals(true);

    ui->RADIO_TYPE->clear();
    ui->RADIO_SETUP->clear();
    ui->comboBox_radioSetup2->clear();

    for (int i=0; i < radioTypes.size(); ++i) {
        ui->RADIO_TYPE->addItem(radioTypes.at(i), i-1);
        ui->RADIO_SETUP->addItem(radioTypes.at(i), i);
        ui->comboBox_radioSetup2->addItem(radioTypes.at(i), i);
    }

    if (idx < ui->RADIO_TYPE->count())
        ui->RADIO_TYPE->setCurrentIndex(idx);
    if (idx2 < ui->RADIO_SETUP->count())
        ui->RADIO_SETUP->setCurrentIndex(idx2);
    if (idx3 < ui->comboBox_radioSetup2->count())
        ui->comboBox_radioSetup2->setCurrentIndex(idx3);

    ui->RADIO_TYPE->blockSignals(false);
    ui->RADIO_SETUP->blockSignals(false);
    ui->comboBox_radioSetup2->blockSignals(false);
}

bool QGCAutoquad::radioHasPPM()
{
    bool hasPPM = (!useRadioSetupParam && ui->RADIO_TYPE->itemData(ui->RADIO_TYPE->currentIndex()).toInt() == 3) ||
            (useRadioSetupParam && (
                ui->RADIO_SETUP->itemData(ui->RADIO_SETUP->currentIndex()).toInt() == 4 ||
                ui->comboBox_radioSetup2->itemData(ui->comboBox_radioSetup2->currentIndex()).toInt() == 4 ));

    return hasPPM;
}

void QGCAutoquad::radioType_changed(int idx) {
    emit hardwareInfoUpdated();

    if (radioHasPPM()) { // PPM
        ui->groupBox_ppmOptions->show();
        ui->groupBox_ppmOptions->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    } else {
        ui->groupBox_ppmOptions->hide();
        ui->groupBox_ppmOptions->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    }

    if (useRadioSetupParam && ui->RADIO_SETUP->currentIndex() > 0 && ui->comboBox_radioSetup2->currentIndex() > 0) {
        ui->comboBox_multiRadioMode->show();
        ui->label_multiRadioMode->show();
    } else {
        ui->comboBox_multiRadioMode->hide();
        ui->label_multiRadioMode->hide();
    }

    if (!paramaq)
        return;

    bool ok;
    int prevRadioValue;
    int newRadioValue;

    if (useRadioSetupParam) {
        prevRadioValue = paramaq->getParaAQ("RADIO_SETUP").toInt(&ok);
        newRadioValue = calcRadioSetting();
    } else {
        prevRadioValue = paramaq->getParaAQ("RADIO_TYPE").toInt(&ok);
        newRadioValue = ui->RADIO_TYPE->itemData(idx).toInt(&ok);
    }

    if (ok && newRadioValue != prevRadioValue)
        ui->label_radioChangeWarning->show();
    else
        ui->label_radioChangeWarning->hide();

}

void QGCAutoquad::on_tab_aq_settings_currentChanged(QWidget *arg1)
{
    bool vis = !(arg1->objectName() == "tab_aq_esc32" || arg1->objectName() == "tab_aq_generate_para");
    ui->lbl_aq_fw_version->setVisible(vis);
    ui->pushButton_save_to_aq->setVisible(vis);
}

void QGCAutoquad::on_groupBox_controlAdvancedSettings_toggled(bool arg1)
{
    ui->widget_controlAdvancedSettings->setVisible(arg1);
}


void QGCAutoquad::on_SPVR_FS_RAD_ST2_currentIndexChanged(int index)
{
    ui->label_SPVR_FS_ADD_ALT->setVisible(index == 2);
    ui->SPVR_FS_ADD_ALT->setVisible(index == 2);
}

void QGCAutoquad::splitterCollapseToggle() {
    QList<int> sz = ui->splitter->sizes();
    static int leftW = qMax(sz.at(0), ui->tabWidget_aq_left->minimumWidth());
    QList<int> newsz;
    if (sz.at(0) > 0) {
        leftW = sz.at(0);
        newsz << 0 << leftW + sz.at(1);
        splitterToggleBtn->setArrowType(Qt::RightArrow);
    } else {
        newsz << leftW << sz.at(1) - leftW;
        splitterToggleBtn->setArrowType(Qt::LeftArrow);
    }
    ui->splitter->setSizes(newsz);
}

void QGCAutoquad::splitterMoved() {
    if (ui->splitter->sizes().at(0) > 0)
        splitterToggleBtn->setArrowType(Qt::LeftArrow);
    else
        splitterToggleBtn->setArrowType(Qt::RightArrow);
}

//void QGCAutoquad::on_groupBox_ppmOptions_toggled(bool arg1)
//{
//    ui->widget_ppmOptions->setVisible(arg1);
//}


// make sure no radio channel assignments conflict
bool QGCAutoquad::validateRadioSettings(int /*idx*/) {
    QList<QString> conflictPorts, portsUsed, essentialPorts;
    QString cbname, cbtxt, xtraChan;

    foreach (QComboBox* cb, allRadioChanCombos) {
        cbname = cb->objectName();
        cbtxt = cb->currentText();
        if (cbname.contains(QRegExp("^(NAV_HDFRE_CHAN|GMBL_PSTHR_CHAN)")))
            continue;
        if (portsUsed.contains(cbtxt))
            conflictPorts.append(cbtxt);
        if (cbname.contains(QRegExp("^RADIO_(THRO|PITC|ROLL|RUDD|FLAP|AUX2)_CH")))
            essentialPorts.append(cbtxt);
        portsUsed.append(cbtxt);
    }
    // validate heading-free controls
    xtraChan = ui->NAV_HDFRE_CHAN->currentText();
    if (ui->NAV_HDFRE_CHAN->currentIndex() && essentialPorts.contains(xtraChan))
        conflictPorts.append(xtraChan);
    // validate passthrough 1
    xtraChan = ui->GMBL_PSTHR_CHAN->currentText();
    if (ui->GMBL_PSTHR_CHAN->currentIndex() && essentialPorts.contains(xtraChan))
        conflictPorts.append(xtraChan);

    foreach (QComboBox* cb, allRadioChanCombos) {
        if (conflictPorts.contains(cb->currentText()))
            cb->setStyleSheet("background-color: rgba(255, 0, 0, 200)");
        else
            cb->setStyleSheet("");
    }

    if (conflictPorts.size())
        return false;

    return true;
}



//
// ESC32
//

void QGCAutoquad::setPortNameEsc32(QString port)
{
    Q_UNUSED(port);

    portNameEsc32 = ui->comboBox_port_esc32->itemData(ui->comboBox_port_esc32->currentIndex()).toString();
//    portNameEsc32 = ui->comboBox_port_esc32->currentText();
//#ifdef Q_OS_WIN
//    portNameEsc32 = portNameEsc32.split("-").first();
//#endif
//    portNameEsc32 = portNameEsc32.remove(" ");
    QString portSpeed = ui->comboBox_esc32PortSpeed->currentText();
    ui->label_portName_esc32->setText(QString("%1 @ %2 bps").arg(portNameEsc32).arg(portSpeed));
    ui->comboBox_port_esc32->setToolTip(ui->comboBox_port_esc32->currentText());
}


void QGCAutoquad::flashFWEsc32() {

    if (esc32)
        esc32->Disconnect();

    ui->textFlashOutput->append(tr("Testing for ESC32 bootloader mode...\n"));

    QProcess stmflash;
    stmflash.setProcessChannelMode(QProcess::MergedChannels);

    QString AppPath = QDir::toNativeSeparators(aqBinFolderPath + "stm32flash" + platformExeExt);

    ui->textFlashOutput->append(AppPath + " " + portName + "\n");

    stmflash.start(AppPath , QStringList() << portName);
    if (!stmflash.waitForFinished(3000)) {
        ui->textFlashOutput->append(tr("stm32flash failed to connect on %1 with error: %2\n").arg(portName).arg(stmflash.errorString()));
        return;
    } else {
        QByteArray stmout = stmflash.readAll();
        //qDebug() << stmout;
        if (stmout.contains("Version")) {
            ui->textFlashOutput->append(tr("ESC32 in bootloader mode already, flashing...\n"));
            flashFwStart();
            return;
        } else {
            ui->textFlashOutput->append(tr("ESC32 not in bootloader mode...\n"));
        }
    }

    esc32 = new AQEsc32();
    connect(esc32, SIGNAL(Esc32Connected()), this, SLOT(Esc32Connected()));
    connect(esc32, SIGNAL(ESc32Disconnected()), this, SLOT(ESc32Disconnected()));
    connect(esc32, SIGNAL(EnteredBootMode()), this, SLOT(Esc32BootModOk()));
    connect(esc32, SIGNAL(NoBootModeArmed(QString)), this, SLOT(Esc32BootModFailure(QString)));
    connect(esc32, SIGNAL(BootModeTimeout()), this, SLOT(Esc32BootModeTimeout()));

    ui->textFlashOutput->append("Attempting to force bootloader mode. Connecting to ESC32...\n");

    FlashEsc32Active = true;
    esc32->Connect(portName, ui->comboBox_esc32PortSpeed->currentText());

}

void QGCAutoquad::Esc32BootModOk() {
    FlashEsc32Active = false;
    esc32->Disconnect();
//    QTimer* tim = new QTimer(this);
//    tim->setSingleShot(true);
//    connect(tim, SIGNAL(timeout()), this, SLOT(flashFwStart()));
//    tim->start(2500);
    flashFwStart();
}

void QGCAutoquad::Esc32BootModFailure(QString err) {
    FlashEsc32Active = false;
    esc32->Disconnect();

    ui->textFlashOutput->append(tr("Failed to enter bootloader mode.\n"));
    if (err.contains("armed"))
        err += tr("\n\nESC appears to be active/armed.  Please disarm first!");
    else
        err += tr("\n\nYou may need to short the BOOT0 pins manually to enter bootloader mode.  Then attempt flashing again.");
    MainWindow::instance()->showCriticalMessage("Error!", err);
}

void QGCAutoquad::Esc32BootModeTimeout() {
    Esc32BootModFailure(tr("Bootloader mode timeout."));
}

void QGCAutoquad::Esc32GotFirmwareVersion(QString ver) {
    ui->label_esc32_fw_version->setText(tr("FW version: %1").arg(ver));
}

void QGCAutoquad::btnConnectEsc32()
{
    if (!esc32) {
        QString port = portNameEsc32;
        QString baud = ui->comboBox_esc32PortSpeed->currentText();

        if (checkAqSerialConnection(port)) {
            QString msg = QString("WARNING: You are already connected to AutoQuad! If you continue, you will be disconnected.\n\nDo you wish to continue connecting to ESC32?").arg(port);
            QMessageBox::StandardButton qrply = QMessageBox::warning(this, tr("Confirm Disconnect AutoQuad"), msg, QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);
            if (qrply == QMessageBox::Cancel)
                return;

            connectedLink->disconnect();
        }

        esc32 = new AQEsc32();
        connect(esc32, SIGNAL(Esc32Connected()), this, SLOT(Esc32Connected()));
        connect(esc32, SIGNAL(ESc32Disconnected()), this, SLOT(ESc32Disconnected()));
        connect(esc32, SIGNAL(getCommandBack(int)), this, SLOT(Esc32CaliGetCommand(int)));
        connect(esc32, SIGNAL(ShowConfig(QString)), this, SLOT(Esc32LoadConfig(QString)));
        connect(esc32, SIGNAL(Esc32ParaWritten(QString)), this, SLOT(ParaWrittenEsc32(QString)));
        connect(esc32, SIGNAL(Esc32CommandWritten(int,QVariant,QVariant)), this, SLOT(CommandWrittenEsc32(int,QVariant,QVariant)));
        connect(esc32, SIGNAL(finishedCalibration(int)), this, SLOT(Esc32CalibrationFinished(int)));
        connect(esc32, SIGNAL(GotFirmwareVersion(QString)), this, SLOT(Esc32GotFirmwareVersion(QString)));

        esc32->Connect(port, baud);
    }
    else {
        esc32->Disconnect();
    }
}

void QGCAutoquad::Esc32LoadConfig(QString Config)
{
    paramEsc32.clear();
    Config.remove(QRegExp("^.*\\[J"));
    Config.remove("\n");
    QStringList RowList = Config.split("\r");
    for ( int j = 0; j< RowList.length(); j++) {
        QStringList ParaList = RowList.at(j).split(" ", QString::SkipEmptyParts);
        if ( ParaList.length() >= 3)
            paramEsc32.insert(ParaList.at(0),ParaList.at(2));
    }

    Esc32ShowConfig(paramEsc32);
    Esc32UpdateStatusText(tr("Loaded current config."));
}

void QGCAutoquad::Esc32ShowConfig(QMap<QString, QString> paramPairs, bool disableMissing) {
    QList<QLineEdit*> edtList = ui->tab_aq_esc32->findChildren<QLineEdit*>(QRegExp("^[A-Z]{2,}") );
    for ( int i = 0; i<edtList.count(); i++) {
        QString ParaName = edtList.at(i)->objectName();
        if ( paramPairs.contains(ParaName) )
        {
            edtList.at(i)->setEnabled(true);
            edtList.at(i)->setText(paramPairs.value(ParaName));
        }
        else if (disableMissing)
            edtList.at(i)->setEnabled(false);
    }

    if (paramPairs.contains("STARTUP_MODE"))
        ui->STARTUP_MODE->setCurrentIndex(paramPairs.value("STARTUP_MODE").toInt());
    if (paramPairs.contains("BAUD_RATE"))
        ui->BAUD_RATE->setCurrentIndex(ui->BAUD_RATE->findText(paramPairs.value("BAUD_RATE")));
    if (paramPairs.contains("ESC_ID"))
        ui->ESC_ID->setCurrentIndex(paramPairs.value("ESC_ID").toInt());
    if (paramPairs.contains("DIRECTION"))
        ui->DIRECTION->setCurrentIndex(paramPairs.value("DIRECTION").toInt() == 1 ? 0 : 1);

    if (disableMissing) {
        ui->groupBox_ESC32_ServoSettings->setVisible(paramPairs.contains("SERVO_DUTY"));
        ui->ESC_ID->setEnabled(paramPairs.contains("ESC_ID"));
        ui->DIRECTION->setEnabled(paramPairs.contains("DIRECTION"));
    }
}

void QGCAutoquad::btnSaveToEsc32() {

    bool something_gos_wrong = false;
    int rettryToStore = 0, timeout = 0;
    QString ParaName, valueText, valueEsc32;
    QMap<QString, QString> changedParams;

    QList<QLineEdit*> edtList = ui->tab_aq_esc32->findChildren<QLineEdit*>();
    for ( int i = 0; i<edtList.count(); i++) {
        ParaName = edtList.at(i)->objectName();
        valueText = edtList.at(i)->text();
        if ( paramEsc32.contains(ParaName) )
        {
            valueEsc32 = paramEsc32.value(ParaName);
            if ( valueEsc32 != valueText || skipParamChangeCheck)
                changedParams.insert(ParaName, valueText);
        }
    }

    valueEsc32 = paramEsc32.value("STARTUP_MODE");
    if (valueEsc32.toInt() != ui->STARTUP_MODE->currentIndex() || skipParamChangeCheck)
        changedParams.insert("STARTUP_MODE", QString::number(ui->STARTUP_MODE->currentIndex()));

    valueEsc32 = paramEsc32.value("BAUD_RATE");
    if (valueEsc32 != ui->BAUD_RATE->currentText() || skipParamChangeCheck)
        changedParams.insert("BAUD_RATE", ui->BAUD_RATE->currentText());

    if ( paramEsc32.contains("ESC_ID") ) {
        valueEsc32 = paramEsc32.value("ESC_ID");
        if (valueEsc32.toInt() != ui->ESC_ID->currentIndex() || skipParamChangeCheck)
            changedParams.insert("ESC_ID", QString::number(ui->ESC_ID->currentIndex()));
    }
    if ( paramEsc32.contains("DIRECTION") ) {
        valueEsc32 = paramEsc32.value("DIRECTION");
        valueText = ui->DIRECTION->currentIndex() == 0 ? "1" : "-1";
        if (valueEsc32 != valueText || skipParamChangeCheck)
            changedParams.insert("DIRECTION", valueText);
    }

    Esc32UpdateStatusText("Writing config...");

    QMapIterator<QString, QString> i(changedParams);
    while (i.hasNext()) {
        i.next();
        WaitForParaWriten = 1;
        ParaNameWritten = i.key();
        esc32->SavePara(ParaNameWritten, i.value());
        timeout = 0;
        while(WaitForParaWriten >0) {
            if (paramEsc32Written.contains(ParaNameWritten)) {
                paramEsc32Written.remove(ParaNameWritten);
                break;
            }
            timeout++;
            if ( timeout > 500000) {
                something_gos_wrong = true;
                rettryToStore++;
                break;
            }
            QCoreApplication::processEvents();
        }
        if ((rettryToStore >= 3) && (something_gos_wrong))
            break;
    }

    if (changedParams.size()&& !something_gos_wrong) {
        skipParamChangeCheck = false;
        Esc32UpdateStatusText(tr("Wrote %1 params.").arg(changedParams.size()));
        saveEEpromEsc32();
    } else if (something_gos_wrong) {
        Esc32UpdateStatusText("Error saving config.");
        MainWindow::instance()->showCriticalMessage("Error!", tr("Something went wrong trying to store the configuration. Please retry!"));
    } else
        Esc32UpdateStatusText("No changed params.");

}

void QGCAutoquad::Esc32SaveParamsToFile()
{
    QString suggestPath = "./esc32.txt";
    if (LastFilePath.length()) {
        QFileInfo fi(LastFilePath);
        suggestPath = fi.absolutePath() + "/" + suggestPath;
    }

    QString fileName = QFileDialog::getSaveFileName(this, tr("Select or Create ESC32 Settings File"), suggestPath,
                                                    tr("Parameter File") + " (*.txt);;" + tr("All File Types") + " (*.*)");
    if (!fileName.length())
        return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        MainWindow::instance()->showCriticalMessage("Error!", tr("Could not open params file. %1").arg(file.errorString()));
        return;
    }

    LastFilePath = fileName;
    Esc32UpdateStatusText("Saving to file...");

    QTextStream in(&file);

    QList<QLineEdit*> edtList = ui->tab_aq_esc32->findChildren<QLineEdit*> (QRegExp("^[A-Z]{2,}"));
    for ( int i = 0; i<edtList.count(); i++) {
        if (edtList.at(i)->text().length())
            in << edtList.at(i)->objectName() << "\t" << edtList.at(i)->text() << "\n";
    }

    in << "STARTUP_MODE" << "\t" << ui->STARTUP_MODE->currentIndex() << "\n";
    in << "BAUD_RATE" << "\t" << ui->BAUD_RATE->currentText() << "\n";
    in << "ESC_ID" << "\t" << ui->ESC_ID->currentIndex() << "\n";
    in << "DIRECTION" << "\t" << (ui->DIRECTION->currentIndex() == 0 ? 1 : -1) << "\n";

    in.flush();
    file.close();

    Esc32UpdateStatusText("Saved to file.");
}

void QGCAutoquad::Esc32LoadParamsFromFile() {
    QString dirPath = QDir::toNativeSeparators(LastFilePath);
    QFileInfo dir(dirPath);

    // use native file dialog
    QString fileName = QFileDialog::getOpenFileName(this, tr("Select Saved Parameters File"), dir.absoluteFilePath(),
                                            tr("Parameter File") + " (*.txt);;" + tr("All File Types") + " (*.*)");

    if (!fileName.length())
        return;

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        MainWindow::instance()->showCriticalMessage("Error!", tr("Could not open params file. %1").arg(file.errorString()));
        return;
    }

    LastFilePath = fileName;

    QTextStream in(&file);
    QMap<QString, QString> loadedParams;
    while (!in.atEnd()) {
        QString line = in.readLine();
        qDebug() << line;
        if (line.contains(QRegExp("^[A-Z\\d_]{2,}[\\t ]+[\\d\\+\\-]"))) {
            QStringList paramPair = line.split(QRegExp("[\\t ]"), QString::SkipEmptyParts);
            qDebug() << paramPair.at(0);
            if (paramPair.size() == 2)
                loadedParams.insert(paramPair.at(0), paramPair.at(1));
        }
    }

    Esc32UpdateStatusText("Loaded from file.");

    Esc32ShowConfig(loadedParams, false);
}

void QGCAutoquad::btnArmEsc32()
{
    if ( !esc32)
        return;
     if ( !esc32_armed)
        esc32->sendCommand(esc32->BINARY_COMMAND_ARM,0.0f, 0.0f, 0, false);
     else
        esc32->sendCommand(esc32->BINARY_COMMAND_DISARM,0.0f, 0.0f, 0, false);

}

void QGCAutoquad::btnStartStopEsc32()
{
    if ( !esc32)
        return;

    if ( !esc32_running)
        esc32->sendCommand(esc32->BINARY_COMMAND_START,0.0f, 0.0f, 0, false);
    else
        esc32->sendCommand(esc32->BINARY_COMMAND_STOP,0.0f, 0.0f, 0, false);
}

void QGCAutoquad::ParaWrittenEsc32(QString ParaName) {
    if ( ParaNameWritten == ParaName) {
        WaitForParaWriten = 0;

        QList<QLineEdit*> edtList = ui->tab_aq_esc32->findChildren<QLineEdit*> ();
        for ( int i = 0; i<edtList.count(); i++) {
            QString ParaNamEedt = edtList.at(i)->objectName();
            if ( ParaNamEedt == ParaName )
            {
                paramEsc32.remove(ParaName);
                paramEsc32.insert(ParaName,edtList.at(i)->text());
                paramEsc32Written.insert(ParaName,edtList.at(i)->text());
                qDebug() << ParaName << " written";
                break;
            }
        }
    }
}

void QGCAutoquad::CommandWrittenEsc32(int CommandName, QVariant V1, QVariant V2) {
    Q_UNUSED(V2);
    switch (CommandName) {
    case AQEsc32::BINARY_COMMAND_ARM :
        ui->pushButton_esc32_read_arm_disarm->setText(tr("disarm"));
        esc32_armed = true;
        break;
    case AQEsc32::BINARY_COMMAND_DISARM :
        ui->pushButton_esc32_read_arm_disarm->setText(tr("arm"));
        esc32_armed = false;
        break;
    case AQEsc32::BINARY_COMMAND_START :
        ui->pushButton_esc32_read_start_stop->setText(tr("stop"));
        esc32_running = true;
        break;
    case AQEsc32::BINARY_COMMAND_STOP :
        ui->pushButton_esc32_read_start_stop->setText(tr("start"));
        esc32_running = false;
        break;
    case AQEsc32::BINARY_COMMAND_RPM :
        ui->spinBox_rpm->setValue(V1.toInt());
        break;
    case AQEsc32::BINARY_COMMAND_CONFIG :
        switch (V1.toInt()) {
        case 0 :
            Esc32UpdateStatusText(tr("Loaded config from flash."));
            skipParamChangeCheck = false;
            break;
        case 1 :
            Esc32UpdateStatusText(tr("Wrote config to flash."));
            skipParamChangeCheck = false;
            break;
        case 2 :
            Esc32UpdateStatusText(tr("Loaded default config."));
            skipParamChangeCheck = true;
            break;
        }
        break;
    }
}

void QGCAutoquad::btnSetRPM()
{
     if (esc32_running && esc32_armed) {
         if ( ui->FF1TERM->text().toFloat() == 0.0f) {
              MainWindow::instance()->showCriticalMessage(tr("Error!"), tr("The Parameter FF1Term is 0.0, can't set the RPM! Please change it and write config to ESC."));
              return;
         }
         float rpm = (float)ui->spinBox_rpm->value();
         esc32->sendCommand(esc32->BINARY_COMMAND_RPM, rpm, 0.0f, 1, false);
    }
}

void QGCAutoquad::saveEEpromEsc32()
{
    QMessageBox msgBox;
    msgBox.setWindowTitle(tr("Question"));
    msgBox.setInformativeText(tr("The values have been transmitted to Esc32! Do you want to store the parameters into permanent memory (ROM)?"));
    msgBox.setWindowModality(Qt::ApplicationModal);
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    int ret = msgBox.exec();
    switch (ret) {
    case QMessageBox::Yes :
        esc32->sendCommand(esc32->BINARY_COMMAND_CONFIG,1.0f, 0.0f, 1, false);
        break;
    default :
        return;
    }
}

void QGCAutoquad::Esc32Connected(){
    if ( !FlashEsc32Active ){
        esc32->CheckVersion();
        esc32->ReadConfigEsc32();
        skipParamChangeCheck = false;
        esc32_connected = true;
    } else {
        ui->textFlashOutput->append(tr("Serial link connected. Attemtping bootloader mode...\n"));
        esc32->SetToBootMode();
    }
    ui->pushButton_connect_to_esc32->setText(tr("disconnect"));
}

void QGCAutoquad::ESc32Disconnected() {
    disconnect(esc32, 0, this, 0);
    esc32 = NULL;
    esc32_connected = false;
    ui->pushButton_connect_to_esc32->setText(tr("connect esc32"));
    ui->label_esc32_fw_version->setText(tr("FW version: [not connected]"));
    Esc32UpdateStatusText(tr("Disconnected."));
}

void QGCAutoquad::Esc32StartLogging() {
    if (!esc32)
        return;

    esc32->StartLogging();
}

void QGCAutoquad::Esc32StartCalibration() {
    if (!esc32)
        return;

    QString Esc32LoggingFile = "";
    QString Esc32ResultFile = "";

     if ( !esc32_calibrating) {
        QMessageBox InfomsgBox;
        InfomsgBox.setText(tr("<p style='color: red; font-weight: bold;'>WARNING!! EXPERIMENTAL FEATURE! BETTER TO USE Linux/OS-X COMMAND-LINE TOOLS!</p> \
<p>This is the calibration routine for ESC32!</p> \
<p>Please be careful with the calibration function! The motor will spin up to full throttle! Please stay clear of the motor & propeller!</p> \
<p><b style='color: red;'>Proceed at your own risk!</b>  You will have one more chance to cancel before the procedure starts.</p>"));
        InfomsgBox.setWindowModality(Qt::ApplicationModal);
        InfomsgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
        InfomsgBox.setDefaultButton(QMessageBox::Cancel);
        int ret = InfomsgBox.exec();
        if (ret == QMessageBox::Cancel)
            return;

        ret = QMessageBox::question(this, tr("Question"), tr("Which calibration do you want to do?"), "RpmToVoltage", "CurrentLimiter");
        if ( ret == 0) {
            Esc32CalibrationMode = 1;
            #ifdef Q_OS_WIN
                Esc32LoggingFile = QDir::toNativeSeparators(QApplication::applicationDirPath() + "\\" + "RPMTOVOLTAGE.txt");
                Esc32ResultFile =  QDir::toNativeSeparators(QApplication::applicationDirPath() + "\\" + "RPMTOVOLTAGE_RESULT.txt");
            #else
                Esc32LoggingFile = QDir::toNativeSeparators(QApplication::applicationDirPath() + "/" + "RPMTOVOLTAGE_RESULT.TXT");
                Esc32ResultFile = QDir::toNativeSeparators(QApplication::applicationDirPath() + "/" + "RPMTOVOLTAGE_RESULT.TXT");
            #endif
        }
        else if ( ret == 1) {
            Esc32CalibrationMode = 2;
            #ifdef Q_OS_WIN
                Esc32LoggingFile = QDir::toNativeSeparators(QApplication::applicationDirPath() + "\\" + "CURRENTLIMITER.TXT");
                Esc32ResultFile = QDir::toNativeSeparators(QApplication::applicationDirPath() + "\\" + "CURRENTLIMITER_RESULT.TXT");
            #else
                Esc32LoggingFile = QDir::toNativeSeparators(QApplication::applicationDirPath() + "/" + "CURRENTLIMITER.TXT");
                Esc32ResultFile = QDir::toNativeSeparators(QApplication::applicationDirPath() + "/" + "CURRENTLIMITER_RESULT.TXT");
            #endif
        }
        else {
            QMessageBox InfomsgBox;
            InfomsgBox.setText("Failure in calibration routine!");
            InfomsgBox.exec();
            return;
        }

        if (QFile::exists(Esc32LoggingFile))
            QFile::remove(Esc32LoggingFile);
        if (QFile::exists(Esc32ResultFile))
            QFile::remove(Esc32ResultFile);

        QMessageBox msgBox;
        msgBox.setText(tr("<p style='font-weight: bold;'>Again, be carful! You can abort using the Stop Calibration button, but the fastest stop is to pull the battery!</p> \
<p style='font-weight: bold;'>To start the calibration procedure, press Yes.</p><p style='color: red; font-weight: bold;'>This is your final warning!</p>"));
        msgBox.setWindowModality(Qt::ApplicationModal);
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Cancel);
        ret = msgBox.exec();
        if ( ret == QMessageBox::Cancel)
            return;

        if ( ret == QMessageBox::Yes) {
            float maxAmps = (float)ui->DoubleMaxCurrent->value();

            esc32->SetCalibrationMode(this->Esc32CalibrationMode);
            esc32->StartCalibration(maxAmps,Esc32LoggingFile,Esc32ResultFile);
            esc32_calibrating = true;
            ui->pushButton_start_calibration->setText(tr("stop calibration"));
        }
    }
     else // stop calibration
    {
        esc32_calibrating = false;
        ui->pushButton_start_calibration->setText(tr("start calibration"));
        esc32->StopCalibration(true);
    }
}

void QGCAutoquad::Esc32CalibrationFinished(int mode) {
    esc32_calibrating = false;
    ui->pushButton_start_calibration->setText(tr("start calibration"));
    //Emergency exit
    if ( mode == 99) {
        //No values from esc32
        QMessageBox InfomsgBox;
        InfomsgBox.setText(tr("Something went wrong in data logging, Aborted!"));
        InfomsgBox.exec();
        return;
    }
    if ( mode == 98) {
        //Abort
        return;
    }
     esc32->StopCalibration(false);
    if ( mode == 1) {
        ui->FF1TERM->setText(QString::number(esc32->getFF1Term()));
          ui->FF2TERM->setText(QString::number(esc32->getFF2Term()));
        //Esc32LoggingFile
        QMessageBox InfomsgBox;
        InfomsgBox.setText(tr("Updated the fields with FF1Term and FF2Term!"));
        InfomsgBox.exec();
        return;
    }
     else if ( mode == 2) {
        ui->CL1TERM->setText(QString::number(esc32->getCL1()));
        ui->CL2TERM->setText(QString::number(esc32->getCL2()));
        ui->CL3TERM->setText(QString::number(esc32->getCL3()));
        ui->CL4TERM->setText(QString::number(esc32->getCL4()));
          ui->CL5TERM->setText(QString::number(esc32->getCL5()));
        QMessageBox InfomsgBox;
        InfomsgBox.setText(tr("Updated the fields with Currentlimiter 1 to Currentlimiter 5!"));
        InfomsgBox.exec();
        return;
    }
}

void QGCAutoquad::btnReadConfigEsc32() {
    Esc32UpdateStatusText(tr("Requesting config..."));
    esc32->ReadConfigEsc32();
    skipParamChangeCheck = false;
    Esc32UpdateStatusText(tr("Loaded current config."));
}

void QGCAutoquad::Esc32LoadDefaultConf() {
    Esc32UpdateStatusText(tr("Loading defaults..."));
    esc32->sendCommand(esc32->BINARY_COMMAND_CONFIG,2.0f, 0.0f, 1, false);
    esc32->ReadConfigEsc32();
}

void QGCAutoquad::Esc32ReLoadConf() {
    Esc32UpdateStatusText(tr("Loading stored config..."));
    esc32->sendCommand(esc32->BINARY_COMMAND_CONFIG,0.0f, 0.0f, 1, false);
    esc32->ReadConfigEsc32();
}

void QGCAutoquad::Esc32CaliGetCommand(int Command){
    esc32->SetCommandBack(Command);
}

void QGCAutoquad::Esc32UpdateStatusText(QString text){
    ui->label_esc32_configStatusText->setText("<i>" + text + "</i>");
    ui->label_esc32_configStatusText->setToolTip(text);
}


//
// FW Flashing
//

void QGCAutoquad::setPortName(QString str)
{
    Q_UNUSED(str);

    portName = ui->portName->itemData(ui->portName->currentIndex()).toString();
//#ifdef Q_OS_WIN
//    portName = portName.split("-").first();
//#endif
//    portName = portName.remove(" ");
//    QString portSpeed = ui->comboBox_fwPortSpeed->currentText();
    ui->portName->setToolTip(ui->portName->currentText());
}

void QGCAutoquad::setupPortList()
{
    QString pdispname;
    QString cidxfw = ui->portName->currentText();
    QString cidxesc = ui->comboBox_port_esc32->currentText();
    ui->portName->clear();
    ui->comboBox_port_esc32->clear();
    // Get the ports available on this system
    foreach (const QextPortInfo &p, QextSerialEnumerator::getPorts()) {
        if (!p.portName.length())
            continue;
        pdispname = p.portName;
        if (p.friendName.length())
            pdispname += " - " + p.friendName.split(QRegExp(" ?\\(")).first();
        ui->portName->addItem(pdispname, p.portName);
        ui->comboBox_port_esc32->addItem(pdispname, p.portName);
    }
    ui->portName->setCurrentIndex(ui->portName->findText(cidxfw));
    ui->comboBox_port_esc32->setCurrentIndex(ui->comboBox_port_esc32->findText(cidxesc));
}

void QGCAutoquad::setFwType() {
    QString typ = "aq";
    // test for esc32 in the fw file name
    if (ui->fileLabel->text().contains(QRegExp("esc32.+\\.hex$", Qt::CaseInsensitive)))
        typ = "esc32";
    // test for aq M4 or v7/8 hardware in fw file name
    else if (ui->fileLabel->text().contains(QRegExp("(aq|autoquad).+(hwv[78]\\.[\\d]|m4).+\\.bin$", Qt::CaseInsensitive)))
        typ = "dfu";

    ui->comboBox_fwType->setCurrentIndex(ui->comboBox_fwType->findData(typ));
}

void QGCAutoquad::fwTypeChange() {
    bool en = ui->comboBox_fwType->itemData(ui->comboBox_fwType->currentIndex()).toString() != "dfu";
    ui->comboBox_fwPortSpeed->setEnabled(en);
    ui->portName->setEnabled(en);
    ui->label_fwPort->setEnabled(en);
    ui->label_fwPortSpeed->setEnabled(en);
    ui->toolButton_fwReloadPorts->setEnabled(en);
    ui->checkBox_verifyFwFlash->setEnabled(en);
}

void QGCAutoquad::selectFWToFlash()
{
    QString dirPath;
    if ( LastFilePath == "")
        dirPath = QCoreApplication::applicationDirPath();
    else
        dirPath = LastFilePath;
    QFileInfo dir(dirPath);

    QString fileName = QFileDialog::getOpenFileName(this, tr("Select Firmware File"), dir.absoluteFilePath(),
                                            tr("AQ or ESC32 firmware") + " (*.hex *.bin)");

    if (fileName.length())
    {
        QString fileNameLocale = QDir::toNativeSeparators(fileName);
        QFile file(fileNameLocale);
        if (!file.open(QIODevice::ReadOnly))
        {
            MainWindow::instance()->showInfoMessage(tr("Warning!"), tr("Could not open firmware file. %1").arg(file.errorString()));
            return;
        }
        ui->fileLabel->setText(fileNameLocale);
        ui->fileLabel->setToolTip(fileNameLocale);
        fileToFlash = file.fileName();
        LastFilePath = fileToFlash;
        file.close();

        setFwType();
    }
}

void QGCAutoquad::flashFW()
{
    if (ui->comboBox_fwType->currentIndex() == -1) {
        MainWindow::instance()->showCriticalMessage(tr("Error!"), tr("Please select the firwmare type (AutoQuad or ESC32)."));
        return;
    }

    if (checkProcRunning())
        return;

    QString fwtype = ui->comboBox_fwType->itemData(ui->comboBox_fwType->currentIndex()).toString();
    QString msg = "";

    if (fwtype == "dfu") {
        msg += tr("Make sure your AQ is connected via USB and is already in bootloader mode.  To enter bootloader mode,"
                  "first connect the BOOT pins (or hold the BOOT button) and then turn the AQ on.\n\n");
#ifndef Q_OS_WIN32
        msg += tr("Please make sure you have the dfu-util program installed on this computer. See http://dfu-util.gnumonks.org/ ."
                  "Mac OS X users should install via MacPorts (http://www.macports.org/).\n\n");
#else
        msg += tr("An automatic restart of AQ will be attempted after flashing, but may not be possible, depending on the USB driver being used. "
                  "You may see an error message at the end of the flash utility output messages, which can be ignored. In this case, simply restart AQ manually.\n\n");
#endif
    } else { // aq and esc serial flash

        if (!portName.length()) {
            MainWindow::instance()->showCriticalMessage(tr("Error!"), tr("Please select an available COM port."));
            return;
        }

        if ( checkAqSerialConnection(portName) )
            msg = tr("WARNING: You are already connected to AutoQuad. If you continue, you will be disconnected and then re-connected afterwards.\n\n");

        if (fwtype == "aq")
            msg += tr("WARNING: Flashing firmware will reset all AutoQuad settings back to default values. Make sure you have your generated parameters and custom settings saved.\n\n");
        else
            msg += tr("WARNING: Flashing firmware will reset all ESC32 settings back to default values. Make sure you have your custom settings saved.\n\n");

        msg += tr("Make sure you are using the %1 port.\n").arg(portName);
        msg += tr("There is a delay before the flashing process shows any progress. Please wait at least 20sec. before you retry!\n\n");
    }
    msg += "Do you wish to continue flashing?";

    QMessageBox::StandardButton qrply = QMessageBox::warning(this, tr("Confirm Firmware Flashing"), msg, QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);
    if (qrply == QMessageBox::Cancel)
        return;

    if (connectedLink)
        connectedLink->disconnect();

    activeProcessStatusWdgt = ui->textFlashOutput;
    fwFlashActive = true;

    if (fwtype == "aq")
        flashFwStart();
    else if (fwtype == "dfu")
        flashFwDfu();
    else
        flashFWEsc32();
}


void QGCAutoquad::flashFwStart()
{
    QString AppPath = QDir::toNativeSeparators(aqBinFolderPath + "stm32flash" + platformExeExt);
    QStringList Arguments;
    Arguments.append(QString("-b"));
    Arguments.append(ui->comboBox_fwPortSpeed->currentText());
    Arguments.append(QString("-w"));
    Arguments.append(QDir::toNativeSeparators(ui->fileLabel->text()));
    if (ui->fileLabel->text().endsWith(".bin", Qt::CaseInsensitive))
        Arguments.append("-s 0x08000000");
    if (ui->checkBox_verifyFwFlash->isChecked())
        Arguments.append("-v");
    Arguments.append(portName);

    QString cmdLine = AppPath;
    foreach (const QString arg, Arguments)
        cmdLine += " " + arg;
    ui->textFlashOutput->append(cmdLine + "\n\n");

    ps_master.start(AppPath , Arguments, QProcess::Unbuffered | QProcess::ReadWrite);
}

void QGCAutoquad::flashFwDfu()
{
#ifdef Q_OS_WIN32
    QString AppPath = QDir::toNativeSeparators(aqBinFolderPath + "dfu-util" + platformExeExt);
#else
    QString AppPath = "dfu-util";
#endif
    QStringList Arguments;
    Arguments.append("-a 0");                   // alt 0 is start of internal flash
    Arguments.append("-d 0483:df11" );          // device ident stm32
    Arguments.append("-s 0x08000000:leave");    // start address (:leave to exit DFU mode after flash)
    //Arguments.append("-v");                   // verbose
    Arguments.append("-R");                     // reset after upload
    Arguments.append("-D");                     // firmware file
    Arguments.append(QDir::toNativeSeparators(ui->fileLabel->text()));

    QString cmdLine = AppPath;
    foreach (const QString arg, Arguments)
        cmdLine += " " + arg;
    ui->textFlashOutput->append(cmdLine + "\n\n");

    ps_master.start(AppPath , Arguments, QProcess::Unbuffered | QProcess::ReadWrite);
}

//
// Radio values view
//

void QGCAutoquad::toggleRadioValuesUpdate() {
    if (!uas) {
        ui->pushButton_toggleRadioGraph->setChecked(false);
        return;
    }

    if (!ui->pushButton_toggleRadioGraph->isChecked()) {
        disconnect(uas, SIGNAL(remoteControlChannelRawChanged(int,float)), this, SLOT(setRadioChannelDisplayValue(int,float)));
        disconnect(uas, SIGNAL(remoteControlChannelScaledChanged(int,float)), this, SLOT(setRadioChannelDisplayValue(int,float)));
        //ui->pushButton_toggleRadioGraph->setText("Start Updating");
        ui->conatiner_radioGraphValues->setEnabled(false);
        return;
    }

    int min, max, tmin, tmax;

    if ( ui->checkBox_raw_value->isChecked() ){
        tmax = 1500;
        tmin = -100;
        max = 1024;
        min = -1024;
        disconnect(uas, SIGNAL(remoteControlChannelScaledChanged(int,float)), this, SLOT(setRadioChannelDisplayValue(int,float)));
        connect(uas, SIGNAL(remoteControlChannelRawChanged(int,float)), this, SLOT(setRadioChannelDisplayValue(int,float)));
    } else {
        tmax = -500;
        tmin = 1500;
        max = -1500;
        min = 1500;
        disconnect(uas, SIGNAL(remoteControlChannelRawChanged(int,float)), this, SLOT(setRadioChannelDisplayValue(int,float)));
        connect(uas, SIGNAL(remoteControlChannelScaledChanged(int,float)), this, SLOT(setRadioChannelDisplayValue(int,float)));
    }

    foreach (QProgressBar* pb, allRadioChanProgressBars) {
        if (pb->objectName().contains("chan_0")) {
            pb->setMaximum(tmax);
            pb->setMinimum(tmin);
        } else {
            pb->setMaximum(max);
            pb->setMinimum(min);
        }
    }

    //ui->pushButton_toggleRadioGraph->setText("Stop Updating");
    ui->conatiner_radioGraphValues->setEnabled(true);

}

void QGCAutoquad::setRadioChannelDisplayValue(int channelId, float normalized)
{
    int val;
    bool raw = ui->checkBox_raw_value->isChecked();
    QString lblTxt;

    if (channelId >= allRadioChanProgressBars.size())
        return;

    // three methods to find the right progress bar...
    // tested on a CoreDuo 3.3GHz at 1-10Hz mavlink refresh, seems to be no practical difference in CPU consumption (~40% ~10Hz)
    QProgressBar* bar = allRadioChanProgressBars.at(channelId);
    QLabel* lbl = allRadioChanValueLabels.at(channelId);
//    QProgressBar* bar = ui->groupBox_Radio_Values->findChild<QProgressBar *>(QString("progressBar_chan_%1").arg(channelId));
//    QLabel* lbl = ui->groupBox_Radio_Values->findChild<QLabel *>(QString("label_chanValue_%1").arg(channelId));
//    QProgressBar* bar = NULL;
//    QLabel* lbl = NULL;

//    switch (channelId) {
//    case 0:
//        bar = ui->progressBar_chan_0;
//        lbl = ui->label_chanValue_0;
//        break;
//    case 1:
//        bar = ui->progressBar_chan_1;
//        lbl = ui->label_chanValue_1;
//        break;
//    case 2:
//        bar = ui->progressBar_chan_2;
//        lbl = ui->label_chanValue_2;
//        break;
//    case 3:
//        bar = ui->progressBar_chan_3;
//        lbl = ui->label_chanValue_3;
//        break;
//    case 4:
//        bar = ui->progressBar_chan_4;
//        lbl = ui->label_chanValue_4;
//        break;
//    case 5:
//        bar = ui->progressBar_chan_5;
//        lbl = ui->label_chanValue_5;
//        break;
//    case 6:
//        bar = ui->progressBar_chan_6;
//        lbl = ui->label_chanValue_6;
//        break;
//    case 7:
//        bar = ui->progressBar_chan_7;
//        lbl = ui->label_chanValue_7;
//        break;
//    }

    if (raw)        // Raw values
        val = (int)(normalized-1024);
    else {    // Scaled values
        val = (int)((normalized*10000.0f)/13);
        if (channelId == 0)
            val += 750;
    }

    if (lbl) {
        lblTxt.sprintf("%+d", val);
        lbl->setText(lblTxt);
    }
    if (bar) {
        if (val > bar->maximum())
            val = bar->maximum();
        if (val < bar->minimum())
            val = bar->minimum();
        bar->setValue(val);
    }
}

void QGCAutoquad::setRssiDisplayValue(float normalized) {
    QProgressBar* bar = ui->progressBar_rssi;
    int val = (int)(normalized);

    if (bar && val <= bar->maximum() && val >= bar->minimum())
        bar->setValue(val);
}

void QGCAutoquad::delayedSendRcRefreshFreq(int rate)
{
    Q_UNUSED(rate);
    delayedSendRCTimer.start();
}

void QGCAutoquad::sendRcRefreshFreq()
{
    delayedSendRCTimer.stop();
    if (!uas)
        return;
    uas->enableRCChannelDataTransmission(ui->spinBox_rcGraphRefreshFreq->value());
}


//
// UAS Interfaces
//

void QGCAutoquad::addUAS(UASInterface* uas_ext)
{
    QString uasColor = uas_ext->getColor().name().remove(0, 1);

}

void QGCAutoquad::setActiveUAS(UASInterface* uas_ext)
{
    if (uas_ext)
    {
        if (uas)
            disconnect(uas, 0, this, 0);
        if (paramaq) {
            disconnect(paramaq, 0, this, 0);
            ui->tabLayout_paramHandler->removeWidget(paramaq);
            delete paramaq;
        }

        uas = uas_ext;
        paramaq = new QGCAQParamWidget(uas, this);
        ui->label_params_no_aq->hide();
        ui->tabLayout_paramHandler->addWidget(paramaq);
        if ( LastFilePath == "")
            paramaq->setFilePath(QCoreApplication::applicationDirPath());
        else
            paramaq->setFilePath(LastFilePath);

        connect(uas, SIGNAL(globalPositionChanged(UASInterface*,double,double,double,quint64)), this, SLOT(globalPositionChangedAq(UASInterface*,double,double,double,quint64)) );
        connect(uas, SIGNAL(textMessageReceived(int,int,int,QString)), this, SLOT(handleStatusText(int, int, int, QString)));
        connect(uas, SIGNAL(remoteControlRSSIChanged(float)), this, SLOT(setRssiDisplayValue(float)));
        //connect(uas, SIGNAL(connected()), this, SLOT(uasConnected())); // this doesn't do anything

        connect(paramaq, SIGNAL(requestParameterRefreshed()), this, SLOT(loadParametersToUI()));
        connect(paramaq, SIGNAL(paramRequestTimeout(int,int)), this, SLOT(paramRequestTimeoutNotify(int,int)));
        connect(paramaq, SIGNAL(parameterListRequested()), this, SLOT(uasConnected()));

        // get firmware version of this AQ
        aqFirmwareVersion = QString("");
        aqFirmwareRevision = 0;
        aqHardwareVersion = 6;
        aqHardwareRevision = 0;
        aqBuildNumber = 0;
        ui->lbl_aq_fw_version->setText("AutoQuad Firmware v. [unknown]");

        paramaq->requestParameterList();

        VisibleWidget = 2;
//        aqTelemetryView->initChart(uas);
        toggleRadioValuesUpdate();
    }
}

UASInterface* QGCAutoquad::getUAS()
{
    return uas;
}

QGCAQParamWidget* QGCAutoquad::getParamHandler()
{
    return paramaq;
}


//
// Parameter handling to/from AQ
//

void QGCAutoquad::getGUIpara(QWidget *parent) {
    if ( !paramaq || !parent)
        return;

    bool ok;
    int precision, tmp;
    QString paraName, valstr;
    QVariant val;
    QLabel *paraLabel;

    // handle all input widgets
    QList<QWidget*> wdgtList = parent->findChildren<QWidget *>(fldnameRx);
    foreach (QWidget* w, wdgtList) {
        paraName = paramNameGuiToOnboard(w->objectName());
        paraLabel = parent->findChild<QLabel *>(QString("label_%1").arg(w->objectName()));

        if (!paramaq->paramExistsAQ(paraName)) {
            w->hide();
            if (paraLabel)
                paraLabel->hide();
            continue;
        }

        w->show();
        if (paraLabel)
            paraLabel->show();
        ok = true;
        precision = 6;
        val = paramaq->getParaAQ(paraName);
        if (paraName == "GMBL_SCAL_PITCH" || paraName == "GMBL_SCAL_ROLL"){
            val = fabs(val.toFloat());
            precision = 8;
        } else if (paraName == "RADIO_SETUP")
            val = val.toInt() & 0x0f;

        if (QLineEdit* le = qobject_cast<QLineEdit *>(w)){
            valstr.setNum(val.toFloat(), 'g', precision);
            le->setText(valstr);
        } else if (QComboBox* cb = qobject_cast<QComboBox *>(w)) {
            if (cb->isEditable()) {
                if ((tmp = cb->findText(val.toString())) > -1)
                    cb->setCurrentIndex(tmp);
                else {
                    cb->insertItem(0, val.toString());
                    cb->setCurrentIndex(0);
                }
            }
            else if ((tmp = cb->findData(val)) > -1)
                cb->setCurrentIndex(tmp);
            else
                cb->setCurrentIndex(abs(val.toInt(&ok)));
        } else if (QDoubleSpinBox* dsb = qobject_cast<QDoubleSpinBox *>(w)) {
            dsb->setValue(val.toDouble(&ok));
        }
        else if (QSpinBox* sb = qobject_cast<QSpinBox *>(w))
            sb->setValue(val.toInt(&ok));
        else
            continue;

        if (ok)
            w->setEnabled(true);
        else
            w->setEnabled(false);
            // TODO: notify the user, or something...
    }

    if (parent->objectName() == "tab_aq_settings") {
        // radio port 2 and mode select boxes
        if (useRadioSetupParam) {
            tmp = paramaq->getParaAQ("RADIO_SETUP").toInt();
            ui->comboBox_radioSetup2->setCurrentIndex(ui->comboBox_radioSetup2->findData((tmp >> 4) & 0x0f));
            ui->comboBox_multiRadioMode->setCurrentIndex(ui->comboBox_multiRadioMode->findData((tmp >> 12) & 0x0f));
        }

        // gimbal pitch/roll revese checkboxes
        ui->reverse_gimbal_pitch->setChecked(paramaq->getParaAQ("GMBL_SCAL_PITCH").toFloat() < 0);
        ui->reverse_gimbal_roll->setChecked(paramaq->getParaAQ("GMBL_SCAL_ROLL").toFloat() < 0);

        on_SPVR_FS_RAD_ST2_currentIndexChanged(ui->SPVR_FS_RAD_ST2->currentIndex());
    }

}

void QGCAutoquad::populateButtonGroups(QObject *parent) {
    QString paraName;
    QVariant val;

    // handle any button groups
    QList<QButtonGroup *> grpList = parent->findChildren<QButtonGroup *>(fldnameRx);
    foreach (QButtonGroup* g, grpList) {
        paraName = g->objectName().replace(dupeFldnameRx, "");
        val = paramaq->getParaAQ(paraName);
//        qDebug() << paraName << val;

        foreach (QAbstractButton* abtn, g->buttons()) {
            if (paramaq->paramExistsAQ(paraName)) {
                abtn->setEnabled(true);
                if (g->exclusive()) { // individual values
                    abtn->setChecked(val.toInt() == g->id(abtn));
                } else { // bitmask
                    abtn->setChecked((val.toInt() & g->id(abtn)));
                }
            } else {
                abtn->setEnabled(false);
            }
        }
    }
}

void QGCAutoquad::loadParametersToUI() {
    mtx_paramsAreLoading = true;
    getGUIpara(ui->tab_aq_settings);
    populateButtonGroups(this);
    aqPwmPortConfig->loadOnboardConfig();

    // check for old PIDs and offer to convert them if running newer firmware
    // TODO: remove me eventually
    if (paramaq->paramExistsAQ("MOT_CAN") &&
            paramaq->getParaAQ("CTRL_TLT_RTE_D").toFloat() < 15000.0f)
        ui->cmdBtn_ConvertTov68AttPIDs->show();
    else
        ui->cmdBtn_ConvertTov68AttPIDs->hide();

    // convert old radio type value if switching to new system
    if (useRadioSetupParam && paramaq->getParaAQ("RADIO_SETUP").toInt() == 0 && paramaq->paramExistsAQ("RADIO_TYPE")) {
        int idx = ui->RADIO_SETUP->findData(paramaq->getParaAQ("RADIO_TYPE").toInt() + 1);
        ui->RADIO_SETUP->setCurrentIndex(idx);
        radioType_changed(idx);
    }

    mtx_paramsAreLoading = false;
}

bool QGCAutoquad::checkAqConnected(bool interactive) {

    if ( !paramaq || !uas || uas->getCommunicationStatus() != uas->COMM_CONNECTED ) {
        if (interactive)
            MainWindow::instance()->showCriticalMessage("Error", "No AutoQuad connected!");
        return false;
    } else
        return true;
}

bool QGCAutoquad::saveSettingsToAq(QWidget *parent, bool interactive)
{
    float val_uas, val_local;
    QString paraName, msg;
    QStringList errors;
    bool ok, chkstate;
    quint8 errLevel = 0;  // 0=no error; 1=soft error; 2=hard error
    QList<float> changeVals;
    QMap<QString, QList<float> > changeList; // param name, old val, new val
    QMessageBox msgBox;
    QVariant tmp;

    if ( !checkAqConnected(interactive) )
        return false;

    QList<QWidget*> wdgtList = parent->findChildren<QWidget *>(fldnameRx);
    QList<QObject*> objList = *reinterpret_cast<QList<QObject *>*>(&wdgtList);
    if (!QString::compare(parent->objectName(), "tab_aq_settings")) {
        QList<QButtonGroup *> grpList = this->findChildren<QButtonGroup *>(fldnameRx);
        objList.append(*reinterpret_cast<QList<QObject *>*>(&grpList));
    }

    foreach (QObject* w, objList) {
        paraName = paramNameGuiToOnboard(w->objectName());

        if (!paramaq->paramExistsAQ(paraName))
            continue;

        ok = true;
        val_uas = paramaq->getParaAQ(paraName).toFloat(&ok);

        if (QLineEdit* le = qobject_cast<QLineEdit *>(w))
            val_local = le->text().toFloat(&ok);
        else if (QComboBox* cb = qobject_cast<QComboBox *>(w)) {
            if (cb->isEditable()) {
                val_local = cb->currentText().toFloat(&ok);
            }
            else {
                tmp = cb->itemData(cb->currentIndex());
                if (tmp.isValid())
                    val_local = tmp.toFloat(&ok);
                else
                    val_local = static_cast<float>(cb->currentIndex());
            }
        } else if (QAbstractSpinBox* sb = qobject_cast<QAbstractSpinBox *>(w))
            val_local = sb->text().replace(QRegExp("[^0-9,\\.-]"), "").toFloat(&ok);
        else if (QButtonGroup* bg = qobject_cast<QButtonGroup *>(w)) {
            val_local = 0;
            foreach (QAbstractButton* abtn, bg->buttons()) {
                if (abtn->isChecked()) {
                    if (bg->exclusive()) {
                        val_local = bg->id(abtn);
                        break;
                    } else
                        val_local += bg->id(abtn);
                }
            }
        }
        else
            continue;

        if (!ok){
            errors.append(paraName);
            continue;
        }

        // special case for reversing gimbal servo direction
        if (paraName == "GMBL_SCAL_PITCH" || paraName == "GMBL_SCAL_ROLL" || paraName == "SIG_BEEP_PRT") {
            if (paraName == "GMBL_SCAL_PITCH")
                chkstate = parent->findChild<QCheckBox *>("reverse_gimbal_pitch")->checkState();
            else if (paraName == "GMBL_SCAL_ROLL")
                chkstate = parent->findChild<QCheckBox *>("reverse_gimbal_roll")->checkState();
            else if (paraName == "SIG_BEEP_PRT")
                chkstate = parent->findChild<QCheckBox *>("checkBox_useSpeaker")->checkState();

            if (chkstate)
                val_local = 0.0f - val_local;
        } else if (paraName == "RADIO_SETUP") {
            val_local = (float)calcRadioSetting();
        }

        // FIXME with a real float comparator
        if (val_uas != val_local) {
            changeVals.clear();
            changeVals.append(val_uas);
            changeVals.append(val_local);
            changeList.insert(paraName, changeVals);
        }
    }

    if (errors.size()) {
        errors.insert(0, tr("One or more parameter(s) could not be saved:"));
        if (errors.size() >= changeList.size())
            errLevel = 2;
        else
            errLevel = 1;
    }

    errLevel = aqPwmPortConfig->saveOnboardConfig(&changeList, &errors);

    if (errLevel) {

        if (errLevel > 1){
            msgBox.setText(tr("Cannot save due to error(s):"));
            msgBox.setStandardButtons(QMessageBox::Close);
            msgBox.setDefaultButton(QMessageBox::Close);
            msgBox.setIcon(QMessageBox::Critical);
        } else {
            msgBox.setText(tr("Possible problem(s) exist:"));
            errors.append(tr("Do you wish to ignore this and continue saving?"));
            msgBox.setStandardButtons(QMessageBox::Ignore | QMessageBox::Cancel);
            msgBox.setDefaultButton(QMessageBox::Cancel);
            msgBox.setIcon(QMessageBox::Warning);
        }
        msgBox.setInformativeText(errors.join("\n\n"));

        int ret = msgBox.exec();
        if (errLevel > 1 || ret == QMessageBox::Cancel)
            return false;

    }

    if ( changeList.size() ) {
        paramSaveType = 1;  // save to volatile
        restartAfterParamSave = false;

        if (interactive) {
            paramSaveType = 0;

            QString msgBoxText = tr("%n parameter(s) modified:\n", "one or more params have changed", changeList.size());
            msg = tr("<table border=\"0\"><thead><tr><th>Parameter </th><th>Old Value </th><th>New Value </th></tr></thead><tbody>\n");
            QMapIterator<QString, QList<float> > i(changeList);
            QString val1, val2;
            while (i.hasNext()) {
                i.next();
                val1.setNum(i.value().at(0), 'g', 8);
                val2.setNum(i.value().at(1), 'g', 8);
                msg += QString("<tr><td style=\"padding: 1px 7px 0 1px;\">%1</td><td>%2 </td><td>%3</td></tr>\n").arg(i.key()).arg(val1).arg(val2);
            }
            msg += "</tbody></table>\n";

            QDialog* dialog = new QDialog(this);
            dialog->setSizeGripEnabled(true);
            dialog->setWindowTitle(tr("Verify Changed Parameters"));
            dialog->setWindowModality(Qt::ApplicationModal);
            dialog->setWindowFlags(dialog->windowFlags() & ~Qt::WindowContextHelpButtonHint);

            QSizePolicy sizepol(QSizePolicy::Expanding, QSizePolicy::Fixed, QSizePolicy::Label);
            sizepol.setVerticalStretch(0);
            QLabel* prompt = new QLabel(msgBoxText, dialog);
            prompt->setSizePolicy(sizepol);
            QLabel* prompt2 = new QLabel(tr("Do you wish to continue?"), dialog);
            prompt2->setSizePolicy(sizepol);

            QCheckBox* restartOption = new QCheckBox(tr("Restart after save?"), dialog);
            restartOption->setToolTip(tr("<html><p>Selecting this option will attempt to automatically restart the flight controller after saving parameters. \
                                         Only do this when saving to permanent memory.  You may loose the link to the flight controller and need to reconnect.</p></html>"));
            restartOption->setObjectName("chkbox_restart");
            restartOption->setSizePolicy(sizepol);
            restartOption->setVisible(aqCanReboot);

            QTextEdit* message = new QTextEdit(msg, dialog);
            message->setReadOnly(true);
            message->setAcceptRichText(true);

            QDialogButtonBox* bbox = new QDialogButtonBox(Qt::Horizontal, dialog);
            QPushButton *btn_saveToRam = bbox->addButton(tr("Save to Volatile Memory"), QDialogButtonBox::AcceptRole);
            btn_saveToRam->setToolTip(tr("The settings will be immediately active and persist UNTIL the flight controller is restarted."));
            btn_saveToRam->setObjectName("btn_saveToRam");
            btn_saveToRam->setAutoDefault(false);
            QPushButton *btn_saveToRom = bbox->addButton(tr("Save to Permanent Memory"), QDialogButtonBox::AcceptRole);
            btn_saveToRom->setToolTip(tr("The settings will be immediately active and persist AFTER flight controller is restarted."));
            btn_saveToRom->setObjectName("btn_saveToRom");
            btn_saveToRom->setAutoDefault(false);
            QPushButton *btn_cancel = bbox->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
            btn_cancel->setToolTip(tr("Do not save any settings."));
            btn_cancel->setDefault(true);

            QVBoxLayout* dlgLayout = new QVBoxLayout(dialog);
            dlgLayout->setSpacing(8);
            dlgLayout->addWidget(prompt);
            dlgLayout->addWidget(message);
            QHBoxLayout* promptLayout = new QHBoxLayout;
            promptLayout->setSpacing(8);
            promptLayout->addWidget(prompt2);
            promptLayout->addWidget(restartOption);
            promptLayout->setAlignment(restartOption, Qt::AlignRight);
            dlgLayout->addLayout(promptLayout);
            dlgLayout->addWidget(bbox);

            dialog->setLayout(dlgLayout);

            connect(btn_cancel, SIGNAL(clicked()), dialog, SLOT(reject()));
            connect(btn_saveToRam, SIGNAL(clicked()), dialog, SLOT(accept()));
            connect(btn_saveToRom, SIGNAL(clicked()), dialog, SLOT(accept()));
            connect(bbox, SIGNAL(clicked(QAbstractButton*)), this, SLOT(saveDialogButtonClicked(QAbstractButton*)));
            connect(restartOption, SIGNAL(clicked(bool)), this, SLOT(saveDialogRestartOptionChecked(bool)));

            bool dlgret = dialog->exec();
            dialog->deleteLater();

            if (dlgret == QDialog::Rejected || !paramSaveType)
                return false;

        }

        QMapIterator<QString, QList<float> > i(changeList);
        while (i.hasNext()) {
            i.next();
            paramaq->setParaAQ(i.key(), i.value().at(1));
        }

        if (paramSaveType == 2) {
            uas->writeParametersToStorageAQ();
            ui->label_radioChangeWarning->hide();
        }

        if (restartAfterParamSave) {
            MainWindow::instance()->showStatusMessage(tr("Restarting flight controller..."), 4000);
            QTimer::singleShot(2000, paramaq, SLOT(restartUas()));
        }

        return true;
    } else {
        if (interactive)
            MainWindow::instance()->showInfoMessage(tr("Warning"), tr("No changed parameters detected.  Nothing saved."));
        return false;
    }
}

void QGCAutoquad::saveAQSettings() {
    if (!validateRadioSettings(0)) {
        MainWindow::instance()->showCriticalMessage(tr("Error"), tr("You have the same port assigned to multiple controls!"));
        return;
    }
    saveSettingsToAq(ui->tab_aq_settings);
}

void QGCAutoquad::saveDialogButtonClicked(QAbstractButton* btn) {
    paramSaveType = 0;
    if (btn->objectName() == "btn_saveToRam")
        paramSaveType = 1;
    else if (btn->objectName() == "btn_saveToRom")
        paramSaveType = 2;
}

void QGCAutoquad::saveDialogRestartOptionChecked(bool chk) {
    restartAfterParamSave = chk;
}

QString QGCAutoquad::paramNameGuiToOnboard(QString paraName) {
    paraName = paraName.replace(dupeFldnameRx, "");

    if (!paramaq)
        return paraName;

    // check for old param names
    QString tmpstr;
    if (paraName.indexOf(QRegExp("NAV_ALT_SPED_.+")) > -1 && !paramaq->paramExistsAQ(paraName)){
        tmpstr = paraName.replace(QRegExp("NAV_ALT_SPED_(.+)"), "NAV_ATL_SPED_\\1");
        if (paramaq->paramExistsAQ(tmpstr))
            paraName = tmpstr;
    }

    // ignore depricated radio_type param
    if (paraName == "RADIO_TYPE" && useRadioSetupParam)
        paraName += "_void";

    return paraName;
}

int QGCAutoquad::calcRadioSetting()
{
    int radioSetup = ui->RADIO_SETUP->itemData(ui->RADIO_SETUP->currentIndex()).toInt() |
                    (ui->comboBox_radioSetup2->itemData(ui->comboBox_radioSetup2->currentIndex()).toInt() << 4) |
                    (ui->comboBox_multiRadioMode->itemData(ui->comboBox_multiRadioMode->currentIndex()).toInt() << 12);

    //qDebug() << radioSetup;
    return radioSetup;
}

void QGCAutoquad::convertPidAttValsToFW68Scales() {
    float v;
    bool ok;
    QList<QLineEdit *> attPIDs = this->findChildren<QLineEdit *>(QRegExp("^CTRL_(TLT_(RTE|ANG)|YAW_RTE)_[PIDOM]{1,2}$"));
    foreach (QLineEdit* le, attPIDs) {
        v = le->text().toFloat(&ok);
        if (ok)
            le->setText(QString::number(v * 4.82f));
    }
    // don't forget CTRL_MAX which is a int spin box
    ui->CTRL_MAX->setValue(ui->CTRL_MAX->value() * 4.82f);

    if (ok) {
        QString msg = tr("<html><p>The <b>Tilt Rate</b>, <b>Tilt Angle</b>, and <b>Yaw Rate</b> PIDs, and the <b>Max. Ctrl. Per Axis</b> (CTRL_MAX) parameter have been converted \
                and are displayed here, but have NOT been sent to AQ (Ctrl. Max. is shown on the Radio & Controls setup screen).</p>\
                <p>To return to the old values, simply refresh the onboard parameters list.</p>\
                <p>Please note that the conversions are approximate. Each value (except the F term!) has been multipled by 4.82 You may want to round some of the numbers a bit.</p>\
                <p>You may also wish to refer to the <a href='http://code.google.com/p/autoquad/source/diff?spec=svn234&r=234&format=side&path=/trunk/onboard/config_default.h#sc_svn233_59'>\
                original code changes</a> for reference.</p></html>");
        MainWindow::instance()->showInfoMessage(tr("Attitude PID values converted."), msg);
        ui->cmdBtn_ConvertTov68AttPIDs->hide();
    }
}



//
// Miscellaneous
//


bool QGCAutoquad::checkProcRunning(bool warn) {
    if (ps_master.state() == QProcess::Running) {
        if (warn)
            MainWindow::instance()->showCriticalMessage(
                        tr("Process already running."),
                        tr("There appears to be an external process (calculation step or firmware flashing) already running. Please abort it first."));
        return true;
    }
    return false;
}

void QGCAutoquad::prtstexit(int stat) {
    prtstdout();
    if ( fwFlashActive ) {  // firmware flashing mode
        ui->flashButton->setEnabled(true);
        if (!stat)
            MainWindow::instance()->showInfoMessage(tr("Restart the device."), tr("Please cycle power to the AQ/ESC or press the AQ reset button to reboot."));
        fwFlashActive = false;
        if (connectedLink) {
            connectedLink->connect();
        }
    }
}

void QGCAutoquad::prtstdout() {
    QString output = ps_master.readAllStandardOutput();
    if (output.contains(QRegExp("\\[(uWrote|H)"))) {
        output = output.replace(QRegExp(".\\[[uH]"), "");
        activeProcessStatusWdgt->clear();
    }
    activeProcessStatusWdgt->insertPlainText(output);
    activeProcessStatusWdgt->ensureCursorVisible();
}


/**
 * @brief Handle external process error code
 * @param err Error code
 */
void QGCAutoquad::extProcessError(QProcess::ProcessError err) {
    QString msg;
    switch(err)
    {
    case QProcess::FailedToStart:
        msg = tr("Failed to start.");
        break;
    case QProcess::Crashed:
        msg = tr("Process terminated (aborted or crashed).");
        break;
    case QProcess::Timedout:
        msg = tr("Timeout waiting for process.");
        break;
    case QProcess::WriteError:
        msg = tr("Cannot write to process, exiting.");
        break;
    case QProcess::ReadError:
        msg = tr("Cannot read from process, exiting.");
        break;
    default:
        msg = tr("Unknown error");
        break;
    }
    activeProcessStatusWdgt->append(msg);
    fwFlashActive = false;
}


void QGCAutoquad::globalPositionChangedAq(UASInterface *, double lat, double lon, double alt, quint64 time){
    Q_UNUSED(time);
    if ( !uas)
        return;
    this->lat = lat;
    this->lon = lon;
    this->alt = alt;
}

void QGCAutoquad::uasConnected() {
    uas->sendCommmandToAq(MAV_CMD_AQ_REQUEST_VERSION, 1);
}

void QGCAutoquad::setHardwareInfo(int boardVer) {
    switch (boardVer) {
     case 8:
        maxPwmPorts = 8;
        pwmPortTimers.empty();
        pwmPortTimers << 3 << 3 << 4 << 4 << 4 << 4 << 8 << 8;
        break;
    case 7:
        maxPwmPorts = 9;
        pwmPortTimers.empty();
        pwmPortTimers << 1 << 1 << 1 << 1 << 4 << 4 << 9 << 9 << 11;
        break;
    case 6:
    default:
        maxPwmPorts = 14;
        pwmPortTimers.empty();
        pwmPortTimers << 1 << 1 << 1 << 1 << 4 << 4 << 4 << 4 << 9 << 9 << 2 << 2 << 10 << 11;
        break;
    }
    emit hardwareInfoUpdated();
}

void QGCAutoquad::setFirmwareInfo() {

    maxMotorPorts = 16;
    motPortTypeCAN = true;
    motPortTypeCAN_H = true;
    useRadioSetupParam = true;
    aqCanReboot = false;

    if (aqBuildNumber) {
        if (aqBuildNumber < 1663)
            motPortTypeCAN_H = false;

        if (aqBuildNumber < 1423)
            maxMotorPorts = 14;

        if (aqBuildNumber < 1418)
            motPortTypeCAN = false;

        if (aqBuildNumber < 1790)
            useRadioSetupParam = false;

        if (aqBuildNumber >= 1740)
            aqCanReboot = true;

    }

    emit firmwareInfoUpdated();
}

QStringList QGCAutoquad::getAvailablePwmPorts(void) {
    QStringList portsList;
    unsigned short maxport = maxPwmPorts;

    if (aqHardwareVersion != 8 && radioHasPPM())
        maxport--;

    for (int i=1; i <= maxport; i++)
        portsList.append(QString::number(i));

    return portsList;
}

void QGCAutoquad::handleStatusText(int uasId, int compid, int severity, QString text) {
    Q_UNUSED(severity);
    Q_UNUSED(compid);
    QRegExp versionRe("^(?:A.*Q.*: )?(\\d+\\.\\d+(?:\\.\\d+)?)([\\s\\-A-Z]*)(?:r(?:ev)?(\\d{1,5}))?(?: b(\\d+))?,?(?: (?:HW ver: (\\d) )?(?:hw)?rev(\\d))?\n?$");
    QString aqFirmwareVersionQualifier;
    bool ok;

    // parse version number
    if (uasId == uas->getUASID() && text.contains(versionRe)) {
        QStringList vlist = versionRe.capturedTexts();
//        qDebug() << vlist.at(1) << vlist.at(2) << vlist.at(3) << vlist.at(4) << vlist.at(5);
        aqFirmwareVersion = vlist.at(1);
        aqFirmwareVersionQualifier = vlist.at(2);
        aqFirmwareVersionQualifier.replace(QString(" "), QString(""));
        if (vlist.at(3).length()) {
            aqFirmwareRevision = vlist.at(3).toInt(&ok);
            if (!ok) aqFirmwareRevision = 0;
        }
        if (vlist.at(4).length()) {
            aqBuildNumber = vlist.at(4).toInt(&ok);
            if (!ok) aqBuildNumber = 0;
        }
        if (vlist.at(5).length()) {
            aqHardwareVersion = vlist.at(5).toInt(&ok);
            if (!ok) aqHardwareVersion = 6;
        }
        if (vlist.at(6).length()) {
            aqHardwareRevision = vlist.at(6).toInt(&ok);
            if (!ok) aqHardwareRevision = -1;
        }

        setHardwareInfo(aqHardwareVersion);
        setFirmwareInfo();

        if (aqFirmwareVersion.length()) {
            QString verStr = QString("AutoQuad FW: v. %1%2").arg(aqFirmwareVersion).arg(aqFirmwareVersionQualifier);
            if (aqFirmwareRevision > 0)
                verStr += QString(" r%1").arg(QString::number(aqFirmwareRevision));
            if (aqBuildNumber > 0)
                verStr += QString(" b%1").arg(QString::number(aqBuildNumber));
            verStr += QString(" HW: v. %1").arg(QString::number(aqHardwareVersion));
            if (aqHardwareRevision > -1)
                verStr += QString(" r%1").arg(QString::number(aqHardwareRevision));

            ui->lbl_aq_fw_version->setText(verStr);
        } else
            ui->lbl_aq_fw_version->setText("AutoQuad Firmware v. [unknown]");
    }
}

bool QGCAutoquad::checkAqSerialConnection(QString port) {
    bool IsConnected = false;
    connectedLink = NULL;

    if (!checkAqConnected(false))
        return false;

    if ( uas != NULL ) {
        for ( int i=0; i < uas->getLinks()->count(); i++) {
            connectedLink = uas->getLinks()->at(i);
            //qDebug() << connectedLink->getName();
            if ( connectedLink->isConnected() == true && (port == "" ||  connectedLink->getName().contains(port))) {
                IsConnected = true;
                break;
            }
        }
    }
    if (!IsConnected)
        connectedLink = NULL;

    return IsConnected;
}

void QGCAutoquad::paramRequestTimeoutNotify(int readCount, int writeCount) {
    MainWindow::instance()->showStatusMessage(tr("PARAMETER READ/WRITE TIMEOUT! Missing: %1 read, %2 write.").arg(readCount).arg(writeCount));
}


//
// Tracking
//


void QGCAutoquad::pushButton_tracking() {
    if ( TrackingIsrunning == 0) {
        QString AppPath = QDir::toNativeSeparators(aqBinFolderPath + "opentld" + platformExeExt);
        QStringList Arguments;

        if ( this->uas == NULL) {
            //qDebug() << "no UAS connected";
            //return;
        }
        OldTrackingMoveX = 0;
        OldTrackingMoveY = 0;

        //globalPositionChanged
        // For video cam
        if ( FileNameForTracking == "" ) {
            Arguments.append("-d CAM");
            Arguments.append("-n " + ui->lineEdit_21->text());
            Arguments.append(aqBinFolderPath + "config.cfg");
        }
        // for Files
        else {
            Arguments.append("-d VID");
            Arguments.append("-i " + FileNameForTracking);
        }
        focal_lenght = ui->lineEdit_20->text().toFloat();
        camera_yaw_offset = ui->lineEdit_19->text().toFloat();
        camera_pitch_offset = ui->lineEdit_18->text().toFloat();
        pixel_size = ui->lineEdit_171->text().toFloat();
        pixelFilterX = ui->lineEdit_22->text().toFloat();
        pixelFilterY = ui->lineEdit_23->text().toFloat();
        currentPosN = 10.75571;
        currentPosE = 48.18003;
        ps_tracking.setWorkingDirectory(aqBinFolderPath);
        ps_tracking.start(AppPath , Arguments, QIODevice::Unbuffered | QIODevice::ReadWrite);
        TrackingIsrunning = 1;
     }
     else {
        OldTrackingMoveX = 0;
        OldTrackingMoveY = 0;

        ps_tracking.kill();
        TrackingIsrunning = 0;
     }

}

void QGCAutoquad::pushButton_tracking_file() {
    QString dirPath = UsersParamsFile ;
    QFileInfo dir(dirPath);
    QFileDialog dialog;
    dialog.setDirectory(dir.absoluteDir());
    dialog.setFileMode(QFileDialog::AnyFile);
    //dialog.setFilter(tr("AQ Parameter-File (*.params)"));
    dialog.setViewMode(QFileDialog::Detail);
    QStringList fileNames;
    if (dialog.exec())
    {
        fileNames = dialog.selectedFiles();
    }

    if (fileNames.size() > 0)
    {
        UsersParamsFile = fileNames.at(0);
    }

    FileNameForTracking = QDir::toNativeSeparators(UsersParamsFile);
    QFile file( FileNameForTracking );
}

void QGCAutoquad::prtstexitTR(int) {
    qDebug() << ps_tracking.readAll();
}

void QGCAutoquad::prtstdoutTR() {
    QString stdout_TR = ps_tracking.readAllStandardOutput();
    QStringList stdout_TR_Array = stdout_TR.split("\r\n",QString::SkipEmptyParts);
    for (int i=0; i < stdout_TR_Array.length(); i++) {
        //qDebug() << stdout_TR_Array.at(i);
        if (stdout_TR_Array.at(i).contains("RESOLUTION=")) {
            int posResolution = stdout_TR_Array.at(i).indexOf("RESOLUTION",0,Qt::CaseSensitive);
            if ( posResolution >= 0) {
                posResolution += 11;
                int posEndOfResolution = stdout_TR_Array.at(i).indexOf("\"",0,Qt::CaseSensitive);
                QString resString = stdout_TR_Array.at(i).mid(posResolution, posEndOfResolution-posResolution);
                SplitRes = resString.split(' ',QString::SkipEmptyParts);
                TrackingResX = SplitRes[0].toInt();
                TrackingResY = SplitRes[1].toInt();
            }
        }
        else if (stdout_TR_Array.at(i).contains("POS=")) {
            int posXMove = stdout_TR_Array.at(i).indexOf("POS=",0,Qt::CaseSensitive);
            if ( posXMove >= 0) {
                posXMove += 4;
                int posEndOfPOS = stdout_TR_Array.at(i).indexOf("\"",0,Qt::CaseSensitive);
                QString resString = stdout_TR_Array.at(i).mid(posXMove, posEndOfPOS-posXMove);
                SplitRes = resString.split(' ',QString::SkipEmptyParts);
                TrackingMoveX = SplitRes[0].toInt();
                TrackingMoveY = SplitRes[1].toInt();
                int DiffX = abs(OldTrackingMoveX - TrackingMoveX);
                int DiffY = abs(OldTrackingMoveY - TrackingMoveY);
                if ((  DiffX > pixelFilterX) || ( DiffY > pixelFilterY)) {
                    OldTrackingMoveX = TrackingMoveX;
                    OldTrackingMoveY = TrackingMoveY;
                    sendTracking();
                    QString output_tracking;
                    output_tracking.append("Xdiff=");
                    output_tracking.append(QString::number(DiffX));
                    output_tracking.append("Ydiff=");
                    output_tracking.append(QString::number(DiffY));
                    //qDebug() << output_tracking;
                    ui->lineEdit_24->setText(output_tracking);
                }

            }
        }
    }
}

void QGCAutoquad::sendTracking(){
    if ( uas != NULL) {
        uas->sendCommmandToAq(9, 1, TrackingMoveX,TrackingMoveY,focal_lenght ,camera_yaw_offset,camera_pitch_offset,pixel_size,0.0f);
    }
    else {
        float yaw, pitch, l1, l2, h, sinp, cotp;
        float Ndev, Edev;
        float PIXEL_SIZE = 7e-6f;
        float FOCAL_LENGTH = 0.02f;
        QString Info;
        h = 0.78;//-UKF_POSD; //Check sign
        Info.append(QString::number(h) + "\t");
        yaw = 0 + 0; //AQ_YAW + CAMERA_YAW_OFFSET;
        Info.append(QString::number(yaw) + "\t");
        pitch = 0 + 1.5707963267949;//0.7854; //AQ_PITCH + CAMERA_PITCH_OFFSET;
        Info.append(QString::number(pitch) + "\t");
        //sinp = std::max(sin(pitch), 0.001f);//safety
        Info.append(QString::number(sinp) + "\t");
        //cotp = std::min(1/tan(pitch), 100.0f);//safety
        Info.append(QString::number(cotp) + "\t");
        l1 = h/sinp;
        Info.append(QString::number(l1) + "\t");
        l2 = h*cotp;
        Info.append(QString::number(l2) + "\t");

        Ndev = TrackingMoveX*PIXEL_SIZE*l1/FOCAL_LENGTH;
        Info.append(QString::number(Ndev) + "\t");
        Info.append(QString::number(TrackingMoveX) + "\t");
        Edev = TrackingMoveY*PIXEL_SIZE*l1/FOCAL_LENGTH;
        Info.append(QString::number(TrackingMoveY) + "\t");
        Info.append(QString::number(Edev) + "\t");

        res1 = l2*cos(yaw) + Ndev;
        Info.append(QString::number(res1) + "\t");
        res2 = l2*sin(yaw) + Edev;
        Info.append(QString::number(res2) + "\t");
        qDebug() << Info;
    }
}

void QGCAutoquad::prtstderrTR() {
    qDebug() << ps_tracking.readAllStandardError();
}

void QGCAutoquad::pushButton_dev1(){
//    QString audiostring = QString("Hello, welcome to AutoQuad");
//    GAudioOutput::instance()->say(audiostring.toLower());
    float headingInDegree = ui->lineEdit_13->text().toFloat();
    uas->sendCommmandToAq(7, 1, headingInDegree,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f);
//    QEventLoop waiter;
//    connect(uas, SIGNAL(textMessageReceived()), &waiter, SLOT(quit()));
//    QTimer::singleShot(5000, &waiter, SLOT(quit()));
//    ui->lineEdit_13->setText("");
//    ui->lineEdit_14->setText("");
//    ui->lineEdit_13->setText(QString::number(aqFirmwareVersion));
//    ui->lineEdit_14->setText(QString::number(aqFirmwareRevision));
//    ui->lineEdit_15->setText(QString::number(aqHardwareRevision));
//    waiter.exec();
}
