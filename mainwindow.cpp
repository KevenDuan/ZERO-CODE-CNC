#include "mainwindow.h"
#include "ui_modern_cnc_dark.h"

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
#include <QPainterPath>
#include <QPen>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStandardItemModel>
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
    , mFeatureSelectAllChecked(true)
{
    ui->setupUi(this);
    setupCadView();
    setupTables();
    setupConnections();
}

MainWindow::~MainWindow()
{
    delete ui;
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

    mFeatureModel->setHorizontalHeaderLabels({"选择", "编号", "类型", "图层", "尺寸", "中心", "置信度", "说明"});
    ui->table_CNC_Features->setModel(mFeatureModel);
    ui->table_CNC_Features->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->table_CNC_Features->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->table_CNC_Features->setAlternatingRowColors(false);
    ui->table_CNC_Features->setFont(tableFont);
    ui->table_CNC_Features->horizontalHeader()->setStretchLastSection(true);
    ui->table_CNC_Features->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
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

    mSequenceModel->setHorizontalHeaderLabels({"选择", "工艺", "目标特征", "说明"});
    ui->table_CNC_Sequence->setModel(mSequenceModel);
    ui->table_CNC_Sequence->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->table_CNC_Sequence->setAlternatingRowColors(false);
    ui->table_CNC_Sequence->setFont(tableFont);
    ui->table_CNC_Sequence->horizontalHeader()->setStretchLastSection(true);
    ui->table_CNC_Sequence->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->table_CNC_Sequence->verticalHeader()->setVisible(false);
    ui->table_CNC_Sequence->setStyleSheet(ui->table_CNC_Features->styleSheet());
}

void MainWindow::setupConnections()
{
    connect(ui->btn_Big_Import, &QPushButton::clicked, this, &MainWindow::importDXF);
    connect(ui->btn_Small_Import, &QPushButton::clicked, this, &MainWindow::importDXF);

    connect(ui->btn_Sub_Edit, &QPushButton::clicked, this, [this]() {
        ui->textBrowser_Log->append("进入图纸编辑/修改模式...");
    });

    connect(ui->btn_Nav_Process, &QPushButton::clicked, this, [this]() { switchMainPage(0); });
    connect(ui->btn_Nav_UserParam, &QPushButton::clicked, this, [this]() { switchMainPage(1); });
    connect(ui->btn_Nav_SysParam, &QPushButton::clicked, this, [this]() { switchMainPage(2); });
    connect(ui->btn_Nav_CNC, &QPushButton::clicked, this, [this]() { switchMainPage(3); });

    connect(ui->btn_CNC_FeatureSelectAll, &QPushButton::clicked, this, [this]() {
        for (int row = 0; row < mFeatureModel->rowCount(); ++row) {
            if (auto *item = mFeatureModel->item(row, 0)) {
                item->setCheckState(mFeatureSelectAllChecked ? Qt::Unchecked : Qt::Checked);
            }
        }
        mFeatureSelectAllChecked = !mFeatureSelectAllChecked;
    });

    connect(ui->btn_CNC_SeqClear, &QPushButton::clicked, this, [this]() {
        mSequenceModel->removeRows(0, mSequenceModel->rowCount());
        ui->textBrowser_Log->append("工艺序列表已清空。");
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
    mSvgItem = new QGraphicsSvgItem(svgPath);
    mCadScene->addItem(mSvgItem);

    ui->stackedWidget_CAD->setCurrentIndex(1);
    switchMainPage(3);
    QCoreApplication::processEvents();

    const QRectF bounds = mSvgItem->boundingRect();
    mCadScene->setSceneRect(bounds);
    ui->graphicsView_CAD->resetTransform();
    ui->graphicsView_CAD->fitInView(bounds, Qt::KeepAspectRatio);
    ui->graphicsView_CAD->scale(0.95, 0.95);

    const QJsonObject result = jsonDoc.object();
    mCurrentFeatures = result.value("features").toArray();
    mCurrentPartBBox = result.value("part_bbox").toObject();
    populateFeatureTable(result);
    appendRecognitionLog(result);
    ui->textBrowser_Log->append("<font color='#6AAB73'>图纸渲染与特征识别完成。</font>");
}

void MainWindow::populateFeatureTable(const QJsonObject &result)
{
    mFeatureModel->removeRows(0, mFeatureModel->rowCount());
    mFeatureSelectAllChecked = false;

    const QJsonObject partBBox = result.value("part_bbox").toObject();
    ui->edit_CNC_PartLength->setText(QString::number(partBBox.value("width").toDouble(), 'f', 3));
    ui->edit_CNC_PartWidth->setText(QString::number(partBBox.value("height").toDouble(), 'f', 3));

    const QJsonArray features = result.value("features").toArray();
    for (int index = 0; index < features.size(); ++index) {
        const QJsonValue &featureValue = features.at(index);
        const QJsonObject feature = featureValue.toObject();
        const QString type = feature.value("type").toString();

        auto *checkItem = new QStandardItem();
        checkItem->setCheckable(true);
        checkItem->setCheckState(Qt::Checked);
        checkItem->setEditable(false);
        checkItem->setTextAlignment(Qt::AlignCenter);

        QList<QStandardItem *> rowItems;
        rowItems << checkItem
                 << makeCell(feature.value("id").toString(), type)
                 << makeCell(type, type)
                 << makeCell(feature.value("layer").toString(), type)
                 << makeCell(formatSizeText(feature), type)
                 << makeCell(formatPointText(feature.value("center").toObject()), type)
                 << makeCell(feature.value("confidence").toString(), type)
                 << makeCell(feature.value("notes").toString(), type);
        mFeatureModel->appendRow(rowItems);

        if (auto *idItem = mFeatureModel->item(index, 1)) {
            idItem->setData(index, Qt::UserRole);
        }
    }

    ui->table_CNC_Features->resizeRowsToContents();
    if (mFeatureModel->rowCount() > 0) {
        ui->table_CNC_Features->selectRow(0);
        updateFeatureHighlight(0);
    }
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
