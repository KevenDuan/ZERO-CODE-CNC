#include "mainwindow.h"
#include "ui_modern_cnc_dark.h"

#include <algorithm>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QGraphicsEllipseItem>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsSvgItem>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QPainterPath>
#include <QPen>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QTimer>
#include <QTextBlock>
#include <QTextEdit>
#include <QMouseEvent>
#include <QWheelEvent>

// 滚轮缩放拦截器
class CADZoomFilter : public QObject {
public:
    explicit CADZoomFilter(QGraphicsView *view) : QObject(view), mView(view) {}

protected:
    bool eventFilter(QObject *obj, QEvent *event) override {
        if (event->type() == QEvent::Wheel) {
            QWheelEvent *wheelEvent = static_cast<QWheelEvent *>(event);
            if (wheelEvent->angleDelta().y() > 0) {
                mView->scale(1.15, 1.15);
            } else {
                mView->scale(1.0 / 1.15, 1.0 / 1.15);
            }
            return true;
        }
        return QObject::eventFilter(obj, event);
    }

private:
    QGraphicsView *mView;
};

static bool extractNcAxisValue(const QString &line, QChar axis, double &value)
{
    const QRegularExpression regex(
        QString(R"((?:^|[^A-Z0-9_])%1\s*([-+]?(?:\d+(?:\.\d*)?|\.\d+)))").arg(axis),
        QRegularExpression::CaseInsensitiveOption
    );
    const QRegularExpressionMatch match = regex.match(line);
    if (!match.hasMatch()) {
        return false;
    }

    bool ok = false;
    const double parsedValue = match.captured(1).toDouble(&ok);
    if (!ok) {
        return false;
    }

    value = parsedValue;
    return true;
}

namespace {
QString confidenceLabelColor(const QString &type)
{
    if (type == "圆孔") {
        return "#4FC3F7";
    }
    if (type == "外轮廓") {
        return "#66BB6A";
    }
    if (type == "内轮廓") {
        return "#FFB74D";
    }
    if (type == "岛") {
        return "#BA68C8";
    }
    return "#EF5350";
}

QStandardItem *makeCell(const QString &text, const QString &type = QString())
{
    auto *item = new QStandardItem(text);
    item->setEditable(false);
    item->setTextAlignment(Qt::AlignCenter);
    if (!type.isEmpty()) {
        item->setForeground(QColor(confidenceLabelColor(type)));
    }
    return item;
}

QStandardItem *makeEditableCell(const QString &text)
{
    auto *item = new QStandardItem(text);
    item->setEditable(true);
    item->setTextAlignment(Qt::AlignCenter);
    return item;
}

QStandardItem *makeCheckableIndexCell(int index, int dataValue)
{
    auto *item = new QStandardItem(QString::number(index));
    item->setCheckable(true);
    item->setCheckState(Qt::Checked);
    item->setEditable(false);
    item->setTextAlignment(Qt::AlignCenter);
    item->setData(dataValue, Qt::UserRole);
    return item;
}
} // namespace

QString MainWindow::resolvePythonScriptPath() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath("../dxf2svg.py"),
        QDir(appDir).filePath("dxf2svg.py"),
        QDir::current().filePath("dxf2svg.py")
    };

    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isFile()) {
            return info.canonicalFilePath();
        }
    }

    return QDir(appDir).filePath("dxf2svg.py");
}

void MainWindow::switchMainPage(int index)
{
    ui->stackedWidget_Main->setCurrentIndex(index);

    ui->btn_Nav_Process->setChecked(index == 0);
    ui->btn_Nav_UserParam->setChecked(index == 1);
    ui->btn_Nav_SysParam->setChecked(index == 2);
    ui->btn_Nav_CNC->setChecked(index == 3);
    ui->btn_Nav_Edit->setChecked(index == 4);
    ui->btn_Nav_Offset->setChecked(index == 5);
    ui->btn_Nav_Magazine->setChecked(index == 6);
    ui->btn_Nav_Tool->setChecked(index == 7);
}

void MainWindow::switchCncPanelPage(int index)
{
    ui->stackedWidget_CNC_Panel->setCurrentIndex(index);
    ui->btn_CNC_ShowFeaturePage->setChecked(index == 0);
    ui->btn_CNC_ShowSequencePage->setChecked(index == 1);
    if (index == 0) {
        QTimer::singleShot(0, this, &MainWindow::updateFeatureTableColumnWidths);
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->table_CNC_Features->viewport() && event->type() == QEvent::MouseButtonRelease) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        const QModelIndex index = ui->table_CNC_Features->indexAt(mouseEvent->pos());
        if (index.isValid() && index.column() == 0) {
            if (QStandardItem *item = mFeatureModel->item(index.row(), 0)) {
                const Qt::CheckState nextState =
                    item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked;
                item->setCheckState(nextState);
                ui->table_CNC_Features->selectRow(index.row());
                return true;
            }
        }
    }

    if ((watched == ui->table_CNC_Features || watched == ui->table_CNC_Features->viewport())
        && event->type() == QEvent::Resize) {
        updateFeatureTableColumnWidths();
    }
    return QMainWindow::eventFilter(watched, event);
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , mCadScene(new QGraphicsScene(this))
    , mFeatureModel(new QStandardItemModel(this))
    , mSequenceModel(new QStandardItemModel(this))
    , mHighlightItem(nullptr)
    , mSvgItem(nullptr)
    , mImportProcess(new QProcess(this))
    , mCurrentNcPath()
    , mCurrentNcLines()
    , mNcLinePositions()
    , mNcFinalPosition()
    , mProcessSimulationTimer(new QTimer(this))
    , mCurrentNcLineIndex(-1)
    , mProcessElapsedSeconds(0)
    , mFeatureSelectAllChecked(true)
    , mSequenceSelectAllChecked(true)
    , mProcessRunning(false)
{
    ui->setupUi(this);
    setupProcessPage();
    setupCadView();
    setupTables();
    setupConnections();
    switchMainPage(0);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setupProcessPage()
{
    ui->widget_ProcessHeader->hide();
    ui->verticalLayout_processPage->setContentsMargins(10, 10, 10, 10);
    ui->verticalLayout_processPage->setSpacing(10);
    ui->verticalLayout_processPage->setStretch(0, 0);
    ui->verticalLayout_processPage->setStretch(1, 11);
    ui->verticalLayout_processPage->setStretch(2, 10);
    ui->horizontalLayout_processCoords->setStretch(0, 1);
    ui->horizontalLayout_processCoords->setStretch(1, 1);
    ui->horizontalLayout_processMachineRemain->setStretch(0, 1);
    ui->horizontalLayout_processMachineRemain->setStretch(1, 1);
    ui->horizontalLayout_processBottom->setStretch(0, 1);
    ui->horizontalLayout_processBottom->setStretch(1, 1);
    ui->gridLayout_processRelative->setColumnStretch(0, 1);
    ui->gridLayout_processRelative->setColumnStretch(1, 3);
    ui->gridLayout_processMachine->setColumnStretch(0, 1);
    ui->gridLayout_processMachine->setColumnStretch(1, 3);
    ui->gridLayout_processRemain->setColumnStretch(0, 1);
    ui->gridLayout_processRemain->setColumnStretch(1, 3);
    ui->gridLayout_processRelative->setRowStretch(0, 1);
    ui->gridLayout_processRelative->setRowStretch(1, 1);
    ui->gridLayout_processRelative->setRowStretch(2, 1);
    ui->gridLayout_processMachine->setRowStretch(0, 1);
    ui->gridLayout_processMachine->setRowStretch(1, 1);
    ui->gridLayout_processMachine->setRowStretch(2, 1);
    ui->gridLayout_processRemain->setRowStretch(0, 1);
    ui->gridLayout_processRemain->setRowStretch(1, 1);
    ui->gridLayout_processRemain->setRowStretch(2, 1);
    ui->horizontalLayout_processHeader->setSpacing(10);
    ui->horizontalSpacer_processHeader->changeSize(8, 20, QSizePolicy::Fixed, QSizePolicy::Minimum);

    const QString axisTitleStyle =
        "background-color: #34373C;"
        "border: none;"
        "border-radius: 10px;"
        "padding: 8px 10px;"
        "font-size: 28px;"
        "font-weight: bold;"
        "color: #C3CDD7;"
        "min-height: 54px;";
    const QString coordValueStyle =
        "background-color: #18211D;"
        "border: 1px solid #2E5640;"
        "border-radius: 12px;"
        "padding: 8px 12px;"
        "font-family: 'Consolas', 'Microsoft YaHei';"
        "font-size: 30px;"
        "font-weight: bold;"
        "color: #63E88A;"
        "min-height: 56px;";
    const QString infoPrimaryStyle =
        "background-color: #20262D;"
        "border: 1px solid #34404C;"
        "border-left: 4px solid #5FB0FF;"
        "border-radius: 10px;"
        "padding: 12px 14px;"
        "font-size: 24px;"
        "font-weight: 700;"
        "color: #E6EEF7;"
        "min-height: 60px;";
    const QString infoSecondaryStyle =
        "background-color: #20262D;"
        "border: 1px solid #34404C;"
        "border-left: 4px solid #58E57A;"
        "border-radius: 10px;"
        "padding: 12px 14px;"
        "font-size: 24px;"
        "font-weight: 700;"
        "color: #E6EEF7;"
        "min-height: 60px;";
    const QString ncFileStyle =
        "background-color: #262C33;"
        "border: none;"
        "border-radius: 10px;"
        "padding: 8px 10px;"
        "font-size: 15px;"
        "font-weight: 700;"
        "color: #B9D9F5;"
        "min-height: 34px;";

    ui->label_ProcessActualTitle->hide();
    ui->label_ProcessParamTitle->hide();
    ui->label_ProcessParamValue->hide();
    ui->label_ProcessLineValue->hide();

    const QList<QLabel *> axisTitleLabels = {
        ui->label_ProcessRelativeXTitle,
        ui->label_ProcessRelativeYTitle,
        ui->label_ProcessRelativeZTitle,
        ui->label_ProcessMachineXTitle,
        ui->label_ProcessMachineYTitle,
        ui->label_ProcessMachineZTitle,
        ui->label_ProcessRemainXTitle,
        ui->label_ProcessRemainYTitle,
        ui->label_ProcessRemainZTitle
    };
    for (QLabel *label : axisTitleLabels) {
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet(axisTitleStyle);
    }

    const QList<QLabel *> coordValueLabels = {
        ui->label_ProcessRelativeXValue,
        ui->label_ProcessRelativeYValue,
        ui->label_ProcessRelativeZValue,
        ui->label_ProcessMachineXValue,
        ui->label_ProcessMachineYValue,
        ui->label_ProcessMachineZValue,
        ui->label_ProcessRemainXValue,
        ui->label_ProcessRemainYValue,
        ui->label_ProcessRemainZValue
    };
    for (QLabel *label : coordValueLabels) {
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet(coordValueStyle);
    }

    ui->label_ProcessNcFileName->setAlignment(Qt::AlignCenter);
    ui->label_ProcessNcFileName->setStyleSheet(ncFileStyle);
    ui->groupBox_ProcessRunInfo->setStyleSheet(
        "QGroupBox#groupBox_ProcessRunInfo {"
        " background-color: #1C2026;"
        " border: 1px solid #313842;"
        " border-radius: 12px;"
        " margin-top: 16px;"
        "}"
        "QGroupBox#groupBox_ProcessRunInfo::title {"
        " subcontrol-origin: margin;"
        " subcontrol-position: top left;"
        " left: 12px;"
        " padding: 0 6px;"
        " color: #9EABB8;"
        " font-size: 14px;"
        " font-weight: 700;"
        "}"
    );
    ui->verticalLayout_processRunInfo->setSpacing(14);
    ui->label_ProcessPartCount->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    ui->label_ProcessPartCount->setStyleSheet(infoPrimaryStyle);
    ui->label_ProcessRunTime->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    ui->label_ProcessRunTime->setStyleSheet(infoSecondaryStyle);
    ui->label_ProcessCycleTime->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    ui->label_ProcessCycleTime->setStyleSheet(infoSecondaryStyle);
    ui->btn_ProcessStartStop->setMinimumHeight(76);
    ui->btn_ProcessStartStop->setStyleSheet(
        "background-color: #238636;"
        "border: 1px solid #34A853;"
        "border-radius: 12px;"
        "color: #F6FFF8;"
        "font-size: 26px;"
        "font-weight: 800;"
    );

    ui->btn_ProcessImportNc->setStyleSheet(
        "background-color: #434B55;"
        "border: 1px solid #58626E;"
        "border-radius: 17px;"
        "color: #E8EEF5;"
        "font-size: 20px;"
        "font-weight: bold;"
    );
    ui->plainTextEdit_ProcessGCode->setStyleSheet(
        "background-color: #1D2127;"
        "border: 1px solid #343A42;"
        "border-radius: 10px;"
        "color: #D7DEE6;"
        "font-family: 'Consolas', 'Microsoft YaHei';"
        "font-size: 22px;"
        "padding: 10px;"
    );

    mProcessSimulationTimer->setInterval(500);
    connect(mProcessSimulationTimer, &QTimer::timeout, this, &MainWindow::advanceProcessSimulation);

    ui->plainTextEdit_ProcessGCode->setLineWrapMode(QTextEdit::NoWrap);
    ui->plainTextEdit_ProcessGCode->setPlaceholderText("未导入 G 代码文件。");
    renderProcessCodeView(-1);
    resetProcessCoordinateDemo();
}

void MainWindow::setupCadView()
{
    ui->graphicsView_CAD->setScene(mCadScene);
    ui->graphicsView_CAD->setDragMode(QGraphicsView::ScrollHandDrag);
    ui->graphicsView_CAD->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    ui->graphicsView_CAD->viewport()->installEventFilter(new CADZoomFilter(ui->graphicsView_CAD));
    ui->graphicsView_CAD->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->graphicsView_CAD->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

void MainWindow::setupTables()
{
    QFont tableFont = ui->table_CNC_Features->font();
    tableFont.setPointSize(9);

    mFeatureModel->setHorizontalHeaderLabels({"序号", "个数", "直径", "孔深", "深度余量", "侧边余量", "工艺"});
    ui->table_CNC_Features->setModel(mFeatureModel);
    ui->table_CNC_Features->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->table_CNC_Features->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->table_CNC_Features->setAlternatingRowColors(false);
    ui->table_CNC_Features->setEditTriggers(
        QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed
    );
    ui->table_CNC_Features->setFont(tableFont);
    ui->table_CNC_Features->installEventFilter(this);
    ui->table_CNC_Features->viewport()->installEventFilter(this);
    auto *featureHeader = ui->table_CNC_Features->horizontalHeader();
    featureHeader->setStretchLastSection(false);
    featureHeader->setSectionResizeMode(QHeaderView::Fixed);
    updateFeatureTableColumnWidths();
    ui->table_CNC_Features->verticalHeader()->setVisible(false);
    ui->table_CNC_Features->setStyleSheet(
        "QTableView {"
        " background-color: #111111;"
        " alternate-background-color: #111111;"
        " color: #D8DEE9;"
        " gridline-color: #2A2A2A;"
        " selection-background-color: #1F3B5B;"
        " selection-color: #FFFFFF;"
        "}"
        "QHeaderView::section {"
        " background-color: #181818;"
        " color: #C7D0D9;"
        " border: 1px solid #2A2A2A;"
        " padding: 4px;"
        " font-size: 9pt;"
        "}"
    );

    mSequenceModel->setHorizontalHeaderLabels(
        {"序号", "方式", "刀具名称", "刀号", "转速", "进给", "刀具步进", "加工深度", "每层切深", "起深", "侧面余量", "底面余量", "加工提示"}
    );
    ui->table_CNC_Sequence->setModel(mSequenceModel);
    ui->table_CNC_Sequence->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->table_CNC_Sequence->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->table_CNC_Sequence->setAlternatingRowColors(false);
    ui->table_CNC_Sequence->setEditTriggers(
        QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed
    );
    ui->table_CNC_Sequence->setFont(tableFont);
    ui->table_CNC_Sequence->horizontalHeader()->setStretchLastSection(true);
    ui->table_CNC_Sequence->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->table_CNC_Sequence->verticalHeader()->setVisible(false);
    ui->table_CNC_Sequence->setStyleSheet(ui->table_CNC_Features->styleSheet());
    QTimer::singleShot(0, this, &MainWindow::updateFeatureTableColumnWidths);
}

void MainWindow::updateFeatureTableColumnWidths()
{
    ui->table_CNC_Features->setColumnWidth(0, 58);
    ui->table_CNC_Features->setColumnWidth(1, 58);
    ui->table_CNC_Features->setColumnWidth(2, 90);
    ui->table_CNC_Features->setColumnWidth(3, 96);
    ui->table_CNC_Features->setColumnWidth(4, 110);
    ui->table_CNC_Features->setColumnWidth(5, 110);

    const int fixedColumnsWidth = 58 + 58 + 90 + 96 + 110 + 110;
    const int processWidth = std::max(98, ui->table_CNC_Features->viewport()->width() - fixedColumnsWidth - 2);
    ui->table_CNC_Features->horizontalHeader()->resizeSection(6, processWidth);
}

void MainWindow::setupConnections()
{
    connect(ui->btn_Big_Import, &QPushButton::clicked, this, &MainWindow::importDXF);
    connect(ui->btn_Small_Import, &QPushButton::clicked, this, &MainWindow::importDXF);
    connect(ui->btn_ProcessImportNc, &QPushButton::clicked, this, &MainWindow::importNCFile);
    connect(ui->btn_ProcessStartStop, &QPushButton::clicked, this, &MainWindow::toggleProcessRunState);

    connect(ui->btn_Nav_Process, &QPushButton::clicked, this, [this]() { switchMainPage(0); });
    connect(ui->btn_Nav_UserParam, &QPushButton::clicked, this, [this]() { switchMainPage(1); });
    connect(ui->btn_Nav_SysParam, &QPushButton::clicked, this, [this]() { switchMainPage(2); });
    connect(ui->btn_Nav_CNC, &QPushButton::clicked, this, [this]() { switchMainPage(3); });
    connect(ui->btn_Nav_Edit, &QPushButton::clicked, this, [this]() { switchMainPage(4); });
    connect(ui->btn_Nav_Offset, &QPushButton::clicked, this, [this]() { switchMainPage(5); });
    connect(ui->btn_Nav_Magazine, &QPushButton::clicked, this, [this]() { switchMainPage(6); });
    connect(ui->btn_Nav_Tool, &QPushButton::clicked, this, [this]() { switchMainPage(7); });

    connect(ui->btn_CNC_ShowFeaturePage, &QPushButton::clicked, this, [this]() { switchCncPanelPage(0); });
    connect(ui->btn_CNC_ShowSequencePage, &QPushButton::clicked, this, [this]() { switchCncPanelPage(1); });

    connect(ui->btn_CNC_FeatureSelectAll, &QPushButton::clicked, this, [this]() {
        for (int row = 0; row < mFeatureModel->rowCount(); ++row) {
            if (auto *item = mFeatureModel->item(row, 0)) {
                item->setCheckState(mFeatureSelectAllChecked ? Qt::Unchecked : Qt::Checked);
            }
        }
        mFeatureSelectAllChecked = !mFeatureSelectAllChecked;
    });

    connect(ui->btn_CNC_GenerateProcess, &QPushButton::clicked, this, &MainWindow::generateSequenceFromSelectedFeatures);

    connect(ui->btn_CNC_SeqSelectAll, &QPushButton::clicked, this, [this]() {
        for (int row = 0; row < mSequenceModel->rowCount(); ++row) {
            if (auto *item = mSequenceModel->item(row, 0)) {
                item->setCheckState(mSequenceSelectAllChecked ? Qt::Unchecked : Qt::Checked);
            }
        }
        mSequenceSelectAllChecked = !mSequenceSelectAllChecked;
    });

    connect(ui->btn_CNC_SeqDelete, &QPushButton::clicked, this, &MainWindow::deleteSelectedSequenceRows);

    connect(ui->btn_CNC_SeqClear, &QPushButton::clicked, this, [this]() {
        mSequenceModel->removeRows(0, mSequenceModel->rowCount());
        mSequenceSelectAllChecked = true;
        ui->textBrowser_Log->append("工艺序列表已清空。");
    });

    connect(ui->btn_CNC_GenerateCode, &QPushButton::clicked, this, [this]() {
        ui->textBrowser_Log->append(
            QString("当前工序列表共 %1 条，代码生成功能可在下一步继续接入。").arg(mSequenceModel->rowCount())
        );
    });

    connect(
        ui->table_CNC_Features->selectionModel(),
        &QItemSelectionModel::currentRowChanged,
        this,
        &MainWindow::highlightFeatureRow
    );

    connect(mImportProcess, &QProcess::finished, this, &MainWindow::handleImportProcessFinished);
    connect(mImportProcess, &QProcess::errorOccurred, this, &MainWindow::handleImportProcessErrorOccurred);
}

QString MainWindow::formatProcessTime(int totalSeconds) const
{
    const int hours = totalSeconds / 3600;
    const int minutes = (totalSeconds % 3600) / 60;
    const int seconds = totalSeconds % 60;
    return QString("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

void MainWindow::resetProcessCoordinateDemo()
{
    updateProcessCoordinateDemo(QVector3D(0.0f, 0.0f, 0.0f));
}

void MainWindow::updateProcessCoordinateDemo(const QVector3D &programPosition)
{
    const QVector3D machineOffset(320.0f, 180.0f, 80.0f);
    const QVector3D machinePosition = machineOffset + programPosition;
    const QVector3D remainPosition = mNcFinalPosition - programPosition;

    auto formatAxis = [](float value) {
        return QString::number(static_cast<double>(value), 'f', 3);
    };

    ui->label_ProcessRelativeXValue->setText(formatAxis(programPosition.x()));
    ui->label_ProcessRelativeYValue->setText(formatAxis(programPosition.y()));
    ui->label_ProcessRelativeZValue->setText(formatAxis(programPosition.z()));

    ui->label_ProcessMachineXValue->setText(formatAxis(machinePosition.x()));
    ui->label_ProcessMachineYValue->setText(formatAxis(machinePosition.y()));
    ui->label_ProcessMachineZValue->setText(formatAxis(machinePosition.z()));

    ui->label_ProcessRemainXValue->setText(formatAxis(remainPosition.x()));
    ui->label_ProcessRemainYValue->setText(formatAxis(remainPosition.y()));
    ui->label_ProcessRemainZValue->setText(formatAxis(remainPosition.z()));
}

void MainWindow::renderProcessCodeView(int highlightLine)
{
    if (mCurrentNcLines.isEmpty()) {
        ui->plainTextEdit_ProcessGCode->setHtml(
            "<div style='font-family:Consolas,\"Microsoft YaHei\";font-size:20px;color:#D7DEE6;'>未导入 G 代码文件。</div>"
        );
        return;
    }

    QStringList htmlLines;
    htmlLines.reserve(mCurrentNcLines.size());
    for (int index = 0; index < mCurrentNcLines.size(); ++index) {
        QString escapedLine = mCurrentNcLines.at(index).toHtmlEscaped();
        escapedLine.replace(" ", "&nbsp;");
        if (escapedLine.isEmpty()) {
            escapedLine = "&nbsp;";
        }

        if (index == highlightLine) {
            htmlLines.append(
                "<div style='background-color:#214331;border:1px solid #4A8E67;border-radius:8px;"
                "padding:8px 12px;color:#98FFB8;font-size:50px;font-weight:900;'>" + escapedLine + "</div>"
            );
        } else {
            htmlLines.append(
                "<div style='padding:5px 8px;color:#D7DEE6;font-size:24px;'>" + escapedLine + "</div>"
            );
        }
    }

    ui->plainTextEdit_ProcessGCode->setHtml(
        "<div style='font-family:Consolas,\"Microsoft YaHei\";background:#1F2329;'>" + htmlLines.join("") + "</div>"
    );

    if (highlightLine >= 0) {
        QTextCursor cursor(ui->plainTextEdit_ProcessGCode->document()->findBlockByLineNumber(highlightLine));
        if (!cursor.isNull()) {
            ui->plainTextEdit_ProcessGCode->setTextCursor(cursor);
            const QRect cursorRect = ui->plainTextEdit_ProcessGCode->cursorRect(cursor);
            QScrollBar *scrollBar = ui->plainTextEdit_ProcessGCode->verticalScrollBar();
            const int targetValue = std::clamp(
                scrollBar->value() + cursorRect.center().y()
                    - ui->plainTextEdit_ProcessGCode->viewport()->height() / 2,
                scrollBar->minimum(),
                scrollBar->maximum()
            );
            scrollBar->setValue(targetValue);
        }
    } else {
        ui->plainTextEdit_ProcessGCode->moveCursor(QTextCursor::Start);
    }
}

void MainWindow::stopProcessSimulation(bool resetHighlight)
{
    mProcessSimulationTimer->stop();
    mProcessRunning = false;
    ui->btn_ProcessStartStop->setText("启动");
    ui->btn_ProcessStartStop->setStyleSheet(
        "background-color: #238636;"
        "border: 1px solid #34A853;"
        "border-radius: 12px;"
        "color: #F6FFF8;"
        "font-size: 26px;"
        "font-weight: 800;"
    );
    ui->label_ProcessRunTime->setText(QString("运行时间: %1").arg(formatProcessTime(mProcessElapsedSeconds)));
    ui->label_ProcessCycleTime->setText(QString("循环时间: %1").arg(formatProcessTime(mProcessElapsedSeconds)));
    if (resetHighlight) {
        mCurrentNcLineIndex = -1;
        renderProcessCodeView(-1);
        resetProcessCoordinateDemo();
    } else if (mCurrentNcLineIndex >= 0 && mCurrentNcLineIndex < mNcLinePositions.size()) {
        updateProcessCoordinateDemo(mNcLinePositions.at(mCurrentNcLineIndex));
    }
}

void MainWindow::advanceProcessSimulation()
{
    if (mCurrentNcLines.isEmpty()) {
        stopProcessSimulation(true);
        return;
    }

    ++mCurrentNcLineIndex;
    if (mCurrentNcLineIndex >= mCurrentNcLines.size()) {
        mCurrentNcLineIndex = mCurrentNcLines.size() - 1;
        renderProcessCodeView(mCurrentNcLineIndex);
        stopProcessSimulation(false);
        return;
    }

    ++mProcessElapsedSeconds;
    renderProcessCodeView(mCurrentNcLineIndex);
    if (mCurrentNcLineIndex >= 0 && mCurrentNcLineIndex < mNcLinePositions.size()) {
        updateProcessCoordinateDemo(mNcLinePositions.at(mCurrentNcLineIndex));
    }
    ui->label_ProcessRunTime->setText(QString("运行时间: %1").arg(formatProcessTime(mProcessElapsedSeconds)));
    ui->label_ProcessCycleTime->setText(QString("循环时间: %1").arg(formatProcessTime(mProcessElapsedSeconds)));
}

void MainWindow::importNCFile()
{
    const QString ncPath = QFileDialog::getOpenFileName(
        this, "导入 G 代码", "", "NC/G代码 (*.nc *.tap *.txt *.gcode *.ngc);;所有文件 (*.*)"
    );

    if (ncPath.isEmpty()) {
        return;
    }

    QFile file(ncPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "导入失败", "无法打开选中的 NC 文件。");
        return;
    }

    const QString codeText = QString::fromUtf8(file.readAll());
    stopProcessSimulation(true);
    mCurrentNcPath = ncPath;
    mCurrentNcLines = codeText.split('\n', Qt::KeepEmptyParts);
    mNcLinePositions.clear();
    QVector3D currentPosition(0.0f, 0.0f, 0.0f);
    for (const QString &line : mCurrentNcLines) {
        double axisValue = 0.0;
        if (extractNcAxisValue(line, 'X', axisValue)) {
            currentPosition.setX(static_cast<float>(axisValue));
        }
        if (extractNcAxisValue(line, 'Y', axisValue)) {
            currentPosition.setY(static_cast<float>(axisValue));
        }
        if (extractNcAxisValue(line, 'Z', axisValue)) {
            currentPosition.setZ(static_cast<float>(axisValue));
        }
        mNcLinePositions.append(currentPosition);
    }
    mNcFinalPosition = mNcLinePositions.isEmpty() ? QVector3D(0.0f, 0.0f, 0.0f) : mNcLinePositions.constLast();
    mProcessElapsedSeconds = 0;
    ui->label_ProcessNcFileName->setText(QFileInfo(ncPath).fileName());
    renderProcessCodeView(-1);
    resetProcessCoordinateDemo();

    int effectiveLineCount = 0;
    for (const QString &line : mCurrentNcLines) {
        if (!line.trimmed().isEmpty()) {
            ++effectiveLineCount;
        }
    }
    ui->label_ProcessPartCount->setText(QString("加工代码行数: %1").arg(effectiveLineCount));
    ui->label_ProcessRunTime->setText("运行时间: 00:00:00");
    ui->label_ProcessCycleTime->setText("循环时间: 00:00:00");
}

void MainWindow::toggleProcessRunState()
{
    if (mCurrentNcLines.isEmpty()) {
        QMessageBox::information(this, "提示", "请先导入 NC 文件。");
        return;
    }

    if (mProcessRunning) {
        stopProcessSimulation(false);
        return;
    }

    mProcessRunning = true;
    mCurrentNcLineIndex = -1;
    mProcessElapsedSeconds = 0;
    ui->btn_ProcessStartStop->setText("停止");
    ui->btn_ProcessStartStop->setStyleSheet(
        "background-color: #AA2E25;"
        "border: 1px solid #D34E45;"
        "border-radius: 12px;"
        "color: #FFF4F2;"
        "font-size: 26px;"
        "font-weight: 800;"
    );
    ui->label_ProcessRunTime->setText("运行时间: 00:00:00");
    ui->label_ProcessCycleTime->setText("循环时间: 00:00:00");
    renderProcessCodeView(-1);
    resetProcessCoordinateDemo();
    advanceProcessSimulation();
    mProcessSimulationTimer->start();
}

void MainWindow::importDXF()
{
    const QString dxfPath = QFileDialog::getOpenFileName(
        this, "导入 CAD 图纸", "", "DXF 图纸 (*.dxf);;所有文件 (*.*)"
    );

    if (dxfPath.isEmpty()) {
        return;
    }

    startDXFImport(dxfPath);
}

void MainWindow::startDXFImport(const QString &dxfPath)
{
    if (mImportProcess->state() != QProcess::NotRunning) {
        ui->textBrowser_Log->append("<font color='#FFB74D'>已有 DXF 识别任务正在运行，请稍候。</font>");
        return;
    }

    const QString tempDir = QDir::tempPath();
    mPendingDxfPath = dxfPath;
    mPendingSvgPath = tempDir + "/kr_temp_render.svg";
    mPendingJsonPath = tempDir + "/kr_temp_features.json";
    const QString scriptPath = resolvePythonScriptPath();

    ui->textBrowser_Log->append("==============================");
    ui->textBrowser_Log->append("开始导入 DXF 并识别加工特征...");
    ui->textBrowser_Log->append("目标文件: " + dxfPath);
    ui->textBrowser_Log->append("识别脚本: " + scriptPath);

    if (!QFileInfo::exists(scriptPath)) {
        QMessageBox::critical(this, "脚本缺失", "未找到识别脚本:\n" + scriptPath);
        return;
    }

    QString pythonProgram = "python";
    if (QFileInfo::exists("D:/miniconda3/python.exe")) {
        pythonProgram = "D:/miniconda3/python.exe";
    }

    mImportProcess->setProgram(pythonProgram);
    mImportProcess->setArguments({scriptPath, dxfPath, mPendingSvgPath, mPendingJsonPath});
    mImportProcess->setProcessChannelMode(QProcess::SeparateChannels);
    setImportUiBusy(true);
    mImportProcess->start();
}

void MainWindow::setImportUiBusy(bool busy)
{
    ui->btn_Big_Import->setEnabled(!busy);
    ui->btn_Small_Import->setEnabled(!busy);
}

void MainWindow::handleImportProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    setImportUiBusy(false);

    const QString output = QString::fromLocal8Bit(mImportProcess->readAllStandardOutput()).trimmed();
    const QString errorOutput = QString::fromLocal8Bit(mImportProcess->readAllStandardError()).trimmed();
    const QString scriptPath = resolvePythonScriptPath();

    if (exitStatus == QProcess::CrashExit || exitCode != 0) {
        QMessageBox::critical(
            this,
            "识别失败",
            "特征识别脚本异常退出。\n\n脚本路径:\n" + scriptPath
            + "\n\n退出码: " + QString::number(exitCode)
            + "\n\n标准输出:\n" + output + "\n\n错误输出:\n" + errorOutput
        );
        ui->textBrowser_Log->append("<font color='#FF5252'>DXF 识别脚本异常退出，已中止本次导入。</font>");
        return;
    }

    if (!output.contains("SUCCESS") || !QFileInfo::exists(mPendingSvgPath) || !QFileInfo::exists(mPendingJsonPath)) {
        QMessageBox::critical(
            this,
            "识别失败",
            "特征识别脚本执行失败。\n\n脚本路径:\n" + scriptPath
            + "\n\n标准输出:\n" + output + "\n\n错误输出:\n" + errorOutput
        );
        ui->textBrowser_Log->append("<font color='#FF5252'>DXF 特征识别失败，请检查脚本路径、Python 环境与依赖。</font>");
        return;
    }

    processImportResult(mPendingSvgPath, mPendingJsonPath);
}

void MainWindow::handleImportProcessErrorOccurred(QProcess::ProcessError error)
{
    if (error == QProcess::Crashed) {
        return;
    }

    setImportUiBusy(false);
    QMessageBox::critical(
        this,
        "识别失败",
        "DXF 识别进程启动或运行失败。\n\n错误: " + QString::number(static_cast<int>(error))
    );
    ui->textBrowser_Log->append("<font color='#FF5252'>DXF 识别进程发生错误，已中止本次导入。</font>");
}

void MainWindow::processImportResult(const QString &svgPath, const QString &jsonPath)
{
    QFile jsonFile(jsonPath);
    if (!jsonFile.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, "结果读取失败", "无法读取特征结果文件:\n" + jsonPath);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonFile.readAll(), &parseError);
    jsonFile.close();

    if (!jsonDoc.isObject()) {
        QMessageBox::critical(
            this,
            "结果格式错误",
            "识别结果 JSON 格式无效。\n"
            + QString("错误位置: %1\n原因: %2").arg(parseError.offset).arg(parseError.errorString())
        );
        return;
    }

    clearFeatureHighlight();
    mSvgItem = nullptr;
    mCadScene->clear();
    mSequenceModel->removeRows(0, mSequenceModel->rowCount());
    mSequenceSelectAllChecked = true;
    mSvgItem = new QGraphicsSvgItem(svgPath);
    mCadScene->addItem(mSvgItem);

    ui->stackedWidget_CAD->setCurrentIndex(1);
    switchMainPage(3);
    switchCncPanelPage(0);
    QCoreApplication::processEvents();

    const QRectF bounds = mSvgItem->boundingRect();
    mCadScene->setSceneRect(bounds);
    ui->graphicsView_CAD->resetTransform();
    ui->graphicsView_CAD->fitInView(bounds, Qt::KeepAspectRatio);
    ui->graphicsView_CAD->scale(0.95, 0.95);

    const QJsonObject result = jsonDoc.object();
    mCurrentFeatures = result.value("features").toArray();
    mCurrentPartBBox = result.value("part_bbox").toObject();
    ui->label_CNC_FileNameValue->setText(result.value("file_name").toString());
    ui->label_CNC_SizeValue->setText(
        QString("%1 x %2")
            .arg(mCurrentPartBBox.value("width").toDouble(), 0, 'f', 3)
            .arg(mCurrentPartBBox.value("height").toDouble(), 0, 'f', 3)
    );
    populateFeatureTable(result);
    appendRecognitionLog(result);
    ui->textBrowser_Log->append("<font color='#6AAB73'>图纸渲染与特征识别完成。</font>");
}

void MainWindow::populateFeatureTable(const QJsonObject &result)
{
    mFeatureModel->removeRows(0, mFeatureModel->rowCount());
    mFeatureSelectAllChecked = false;

    const QJsonArray features = result.value("features").toArray();
    for (int index = 0; index < features.size(); ++index) {
        const QJsonValue &featureValue = features.at(index);
        const QJsonObject feature = featureValue.toObject();
        const QString diameterText = feature.contains("diameter")
            ? QString::number(feature.value("diameter").toDouble(), 'f', 3)
            : "-";

        QList<QStandardItem *> rowItems;
        rowItems << makeCheckableIndexCell(index + 1, index)
                 << makeCell("1")
                 << makeCell(diameterText)
                 << makeEditableCell("0.000")
                 << makeEditableCell("0.000")
                 << makeEditableCell("0.000")
                 << makeEditableCell(defaultProcessForFeature(feature));
        mFeatureModel->appendRow(rowItems);
    }

    ui->table_CNC_Features->resizeRowsToContents();
    updateFeatureTableColumnWidths();
    ui->table_CNC_Features->viewport()->update();
    if (mFeatureModel->rowCount() > 0) {
        ui->table_CNC_Features->selectRow(0);
        updateFeatureHighlight(0);
    }
}

void MainWindow::generateSequenceFromSelectedFeatures()
{
    const QList<int> rows = checkedRows(mFeatureModel);
    if (rows.isEmpty()) {
        ui->textBrowser_Log->append("<font color='#FFB74D'>请先在列表页面勾选要生成工艺的项目。</font>");
        return;
    }

    mSequenceModel->removeRows(0, mSequenceModel->rowCount());

    for (int sequenceIndex = 0; sequenceIndex < rows.size(); ++sequenceIndex) {
        const int row = rows.at(sequenceIndex);
        const auto *indexItem = mFeatureModel->item(row, 0);
        if (!indexItem) {
            continue;
        }
        const int featureIndex = indexItem->data(Qt::UserRole).toInt();
        if (featureIndex < 0 || featureIndex >= mCurrentFeatures.size()) {
            continue;
        }

        const QJsonObject feature = mCurrentFeatures.at(featureIndex).toObject();
        const QString process = mFeatureModel->item(row, 6) ? mFeatureModel->item(row, 6)->text() : defaultProcessForFeature(feature);
        const QString depth = mFeatureModel->item(row, 3) ? mFeatureModel->item(row, 3)->text() : "0.000";
        const QString depthAllowance = mFeatureModel->item(row, 4) ? mFeatureModel->item(row, 4)->text() : "0.000";
        const QString sideAllowance = mFeatureModel->item(row, 5) ? mFeatureModel->item(row, 5)->text() : "0.000";
        const bool isHole = feature.value("type").toString() == "圆孔";

        QList<QStandardItem *> rowItems;
        rowItems << makeCheckableIndexCell(sequenceIndex + 1, featureIndex)
                 << makeEditableCell(process)
                 << makeEditableCell(defaultToolNameForFeature(feature))
                 << makeEditableCell(isHole ? "T01" : "T02")
                 << makeEditableCell(isHole ? "12000" : "10000")
                 << makeEditableCell(isHole ? "500" : "1200")
                 << makeEditableCell(isHole ? "0.000" : "2.000")
                 << makeEditableCell(depth)
                 << makeEditableCell(isHole ? "1.000" : "0.500")
                 << makeEditableCell("0.000")
                 << makeEditableCell(sideAllowance)
                 << makeEditableCell(depthAllowance)
                 << makeEditableCell(QString("%1 %2").arg(feature.value("id").toString(), feature.value("notes").toString()).trimmed());
        mSequenceModel->appendRow(rowItems);
    }

    mSequenceSelectAllChecked = false;
    ui->table_CNC_Sequence->resizeColumnsToContents();
    ui->table_CNC_Sequence->resizeRowsToContents();
    switchCncPanelPage(1);
    ui->textBrowser_Log->append(QString("已生成 %1 条工序。").arg(mSequenceModel->rowCount()));
}

void MainWindow::deleteSelectedSequenceRows()
{
    QList<int> rows = checkedRows(mSequenceModel);
    if (rows.isEmpty()) {
        const QModelIndexList selected = ui->table_CNC_Sequence->selectionModel()->selectedRows();
        for (const QModelIndex &index : selected) {
            rows.append(index.row());
        }
    }

    if (rows.isEmpty()) {
        ui->textBrowser_Log->append("<font color='#FFB74D'>请先勾选或选中要删除的工序。</font>");
        return;
    }

    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

    for (int i = rows.size() - 1; i >= 0; --i) {
        mSequenceModel->removeRow(rows.at(i));
    }

    for (int row = 0; row < mSequenceModel->rowCount(); ++row) {
        if (auto *item = mSequenceModel->item(row, 0)) {
            item->setText(QString::number(row + 1));
        }
    }

    mSequenceSelectAllChecked = true;
    ui->textBrowser_Log->append("已删除选中的工序。");
}

void MainWindow::appendRecognitionLog(const QJsonObject &result)
{
    const QString fileName = result.value("file_name").toString();
    const int featureCount = result.value("feature_count").toInt();
    ui->textBrowser_Log->append(QString("识别文件: %1").arg(fileName));
    ui->textBrowser_Log->append(QString("识别到 %1 个加工特征。").arg(featureCount));

    const QJsonObject summary = result.value("feature_summary").toObject();
    for (auto it = summary.begin(); it != summary.end(); ++it) {
        ui->textBrowser_Log->append(QString("  - %1: %2").arg(it.key()).arg(it.value().toInt()));
    }

    const QJsonObject entitySummary = result.value("entity_summary").toObject();
    if (!entitySummary.isEmpty()) {
        ui->textBrowser_Log->append("DXF 实体统计:");
        for (auto it = entitySummary.begin(); it != entitySummary.end(); ++it) {
            ui->textBrowser_Log->append(QString("  - %1: %2").arg(it.key()).arg(it.value().toInt()));
        }
    }

    const QJsonObject unsupportedEntities = result.value("unsupported_entities").toObject();
    if (!unsupportedEntities.isEmpty()) {
        ui->textBrowser_Log->append("<font color='#FFB74D'>未处理实体:</font>");
        for (auto it = unsupportedEntities.begin(); it != unsupportedEntities.end(); ++it) {
            ui->textBrowser_Log->append(
                QString("<font color='#FFB74D'>  - %1: %2</font>").arg(it.key()).arg(it.value().toInt())
            );
        }
    }

    const QJsonArray warnings = result.value("warnings").toArray();
    for (const QJsonValue &warning : warnings) {
        ui->textBrowser_Log->append(QString("<font color='#FFB74D'>警告: %1</font>").arg(warning.toString()));
    }
}

QList<int> MainWindow::checkedRows(const QStandardItemModel *model) const
{
    QList<int> rows;
    if (!model) {
        return rows;
    }

    for (int row = 0; row < model->rowCount(); ++row) {
        const QStandardItem *item = model->item(row, 0);
        if (item && item->isCheckable() && item->checkState() == Qt::Checked) {
            rows.append(row);
        }
    }
    return rows;
}

QString MainWindow::defaultProcessForFeature(const QJsonObject &feature) const
{
    const QString type = feature.value("type").toString();
    if (type == "圆孔") {
        return "钻孔";
    }
    if (type == "开放轮廓") {
        return "开放轮廓铣";
    }
    if (type == "外轮廓" || type == "内轮廓" || type == "岛") {
        return "轮廓铣";
    }
    return "通用加工";
}

QString MainWindow::defaultToolNameForFeature(const QJsonObject &feature) const
{
    const QString type = feature.value("type").toString();
    if (type == "圆孔") {
        const double diameter = feature.value("diameter").toDouble();
        if (diameter > 0.0) {
            return QString("钻刀 Φ%1").arg(diameter, 0, 'f', 3);
        }
        return "钻刀";
    }

    const double diameter = feature.value("diameter").toDouble();
    if (diameter > 0.0) {
        return QString("立铣刀 Φ%1").arg(diameter, 0, 'f', 3);
    }
    return "立铣刀 Φ6.000";
}

QString MainWindow::formatPointText(const QJsonObject &point) const
{
    const double x = point.value("x").toDouble();
    const double y = point.value("y").toDouble();
    return QString("(%1, %2)").arg(x, 0, 'f', 3).arg(y, 0, 'f', 3);
}

QPointF MainWindow::mapDxfPointToScene(const QJsonObject &point) const
{
    if (!mSvgItem || mCurrentPartBBox.isEmpty()) {
        return {};
    }

    const double minX = mCurrentPartBBox.value("min_x").toDouble();
    const double minY = mCurrentPartBBox.value("min_y").toDouble();
    const double width = mCurrentPartBBox.value("width").toDouble();
    const double height = mCurrentPartBBox.value("height").toDouble();
    if (width <= 0.0 || height <= 0.0) {
        return {};
    }

    const QRectF svgBounds = mSvgItem->boundingRect();
    const double scaleX = svgBounds.width() / width;
    const double scaleY = svgBounds.height() / height;

    const double normalizedX = (point.value("x").toDouble() - minX) * scaleX + svgBounds.left();
    const double normalizedY = svgBounds.bottom() - (point.value("y").toDouble() - minY) * scaleY;
    return {normalizedX, normalizedY};
}

QString MainWindow::formatSizeText(const QJsonObject &feature) const
{
    if (feature.contains("diameter")) {
        return QString("D%1").arg(feature.value("diameter").toDouble(), 0, 'f', 3);
    }

    const QJsonObject bbox = feature.value("bbox").toObject();
    const double width = bbox.value("width").toDouble();
    const double height = bbox.value("height").toDouble();

    if (feature.contains("length")) {
        return QString("L%1").arg(feature.value("length").toDouble(), 0, 'f', 3);
    }

    return QString("%1 x %2").arg(width, 0, 'f', 3).arg(height, 0, 'f', 3);
}

void MainWindow::highlightFeatureRow(const QModelIndex &current, const QModelIndex &previous)
{
    Q_UNUSED(previous);

    if (!current.isValid()) {
        clearFeatureHighlight();
        return;
    }

    updateFeatureHighlight(current.row());
}

void MainWindow::updateFeatureHighlight(int row)
{
    clearFeatureHighlight();

    if (row < 0 || row >= mCurrentFeatures.size() || !mSvgItem) {
        return;
    }

    const QJsonObject feature = mCurrentFeatures.at(row).toObject();
    const QJsonArray points = feature.value("points").toArray();
    if (points.isEmpty() || mCurrentPartBBox.isEmpty()) {
        return;
    }

    QPainterPath highlightPath;
    bool firstPoint = true;

    for (const QJsonValue &pointValue : points) {
        const QJsonObject point = pointValue.toObject();
        const QPointF scenePoint = mapDxfPointToScene(point);
        if (!qIsFinite(scenePoint.x()) || !qIsFinite(scenePoint.y())) {
            continue;
        }
        if (firstPoint) {
            highlightPath.moveTo(scenePoint);
            firstPoint = false;
        } else {
            highlightPath.lineTo(scenePoint);
        }
    }

    if (firstPoint || highlightPath.isEmpty()) {
        return;
    }

    if (feature.value("closed").toBool()) {
        highlightPath.closeSubpath();
    }

    QPen pen(QColor("#00E5FF"));
    pen.setWidthF(2.2);
    pen.setCosmetic(true);

    mHighlightItem = mCadScene->addPath(highlightPath, pen);
    mHighlightItem->setZValue(1000.0);

    if (!feature.value("closed").toBool()) {
        const QPointF startPoint = mapDxfPointToScene(feature.value("start_point").toObject());
        const QPointF endPoint = mapDxfPointToScene(feature.value("end_point").toObject());
        const qreal markerRadius = 5.0;

        QPen startPen(QColor("#66BB6A"));
        startPen.setCosmetic(true);
        startPen.setWidthF(2.0);
        QBrush startBrush(QColor("#66BB6A"));
        auto *startMarker = mCadScene->addEllipse(
            startPoint.x() - markerRadius,
            startPoint.y() - markerRadius,
            markerRadius * 2.0,
            markerRadius * 2.0,
            startPen,
            startBrush
        );
        startMarker->setZValue(1001.0);
        mHighlightMarkers.append(startMarker);

        QPen endPen(QColor("#EF5350"));
        endPen.setCosmetic(true);
        endPen.setWidthF(2.0);
        QBrush endBrush(QColor("#EF5350"));
        auto *endMarker = mCadScene->addEllipse(
            endPoint.x() - markerRadius,
            endPoint.y() - markerRadius,
            markerRadius * 2.0,
            markerRadius * 2.0,
            endPen,
            endBrush
        );
        endMarker->setZValue(1001.0);
        mHighlightMarkers.append(endMarker);

        QPen gapPen(QColor("#FFD54F"));
        gapPen.setCosmetic(true);
        gapPen.setWidthF(1.6);
        gapPen.setStyle(Qt::DashLine);
        auto *gapLine = mCadScene->addLine(QLineF(startPoint, endPoint), gapPen);
        gapLine->setZValue(999.5);
        mHighlightMarkers.append(gapLine);
    }
}

void MainWindow::clearFeatureHighlight()
{
    if (mHighlightItem) {
        if (QGraphicsScene *scene = mHighlightItem->scene()) {
            scene->removeItem(mHighlightItem);
        }
        delete mHighlightItem;
        mHighlightItem = nullptr;
    }

    for (QGraphicsItem *item : std::as_const(mHighlightMarkers)) {
        if (!item) {
            continue;
        }
        if (QGraphicsScene *scene = item->scene()) {
            scene->removeItem(item);
        }
        delete item;
    }
    mHighlightMarkers.clear();
}
