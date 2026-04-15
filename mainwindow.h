#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QJsonArray>
#include <QMainWindow>
#include <QJsonObject>
#include <QList>
#include <QProcess>

// 提前声明 UI 命名空间，这是 Qt 的标准做法
QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QGraphicsScene;
class QGraphicsItem;
class QGraphicsPathItem;
class QGraphicsSvgItem;
class QStandardItemModel;

class MainWindow : public QMainWindow
{
    Q_OBJECT // 这个宏非常重要，有了它才能使用信号与槽机制

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    QString resolvePythonScriptPath() const;
    void switchMainPage(int index);
    void switchCncPanelPage(int index);
    void setupCadView();
    void setupTables();
    void setupConnections();
    void importDXF();
    void startDXFImport(const QString &dxfPath);
    void setImportUiBusy(bool busy);
    void handleImportProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleImportProcessErrorOccurred(QProcess::ProcessError error);
    void processImportResult(const QString &svgPath, const QString &jsonPath);
    void populateFeatureTable(const QJsonObject &result);
    void appendRecognitionLog(const QJsonObject &result);
    void generateSequenceFromSelectedFeatures();
    void deleteSelectedSequenceRows();
    void highlightFeatureRow(const QModelIndex &current, const QModelIndex &previous);
    void updateFeatureHighlight(int row);
    void clearFeatureHighlight();
    QList<int> checkedRows(const QStandardItemModel *model) const;
    QString defaultProcessForFeature(const QJsonObject &feature) const;
    QString defaultToolNameForFeature(const QJsonObject &feature) const;
    QPointF mapDxfPointToScene(const QJsonObject &point) const;
    QString formatPointText(const QJsonObject &point) const;
    QString formatSizeText(const QJsonObject &feature) const;

    Ui::MainWindow *ui; // 指向由 .ui 文件自动生成的界面类指针
    QGraphicsScene *mCadScene;
    QStandardItemModel *mFeatureModel;
    QStandardItemModel *mSequenceModel;
    QGraphicsPathItem *mHighlightItem;
    QList<QGraphicsItem *> mHighlightMarkers;
    QGraphicsSvgItem *mSvgItem;
    QProcess *mImportProcess;
    QJsonArray mCurrentFeatures;
    QJsonObject mCurrentPartBBox;
    QString mPendingDxfPath;
    QString mPendingSvgPath;
    QString mPendingJsonPath;
    bool mFeatureSelectAllChecked;
    bool mSequenceSelectAllChecked;
};

#endif // MAINWINDOW_H
