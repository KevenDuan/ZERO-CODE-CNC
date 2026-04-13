#include "mainwindow.h"
#include "ui_modern_cnc_dark.h" 
#include <QFileDialog>
#include <QMessageBox>
#include <QGraphicsScene>
#include <QProcess>            
#include <QGraphicsSvgItem>    
#include <QDir>
#include <QCoreApplication>
#include <QWheelEvent>         
#include <QEvent>              

// ==========================================
// 滚轮缩放拦截器
// ==========================================
class CADZoomFilter : public QObject {
public:
    CADZoomFilter(QGraphicsView* view) : QObject(view), mView(view) {}
protected:
    bool eventFilter(QObject *obj, QEvent *event) override {
        if (event->type() == QEvent::Wheel) {
            QWheelEvent *wheelEvent = static_cast<QWheelEvent*>(event);
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
    QGraphicsView* mView;
};

// ==========================================
// 主窗口逻辑
// ==========================================
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    // 初始化 UI
    ui->setupUi(this);

    // 1. 初始化 CAD 绘图引擎
    QGraphicsScene *cadScene = new QGraphicsScene(this);
    ui->graphicsView_CAD->setScene(cadScene);
    
    // 开启左键拖拽平移与鼠标滚轮智能缩放
    ui->graphicsView_CAD->setDragMode(QGraphicsView::ScrollHandDrag);
    ui->graphicsView_CAD->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    ui->graphicsView_CAD->viewport()->installEventFilter(new CADZoomFilter(ui->graphicsView_CAD));

    ui->graphicsView_CAD->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->graphicsView_CAD->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    // ==========================================
    // 2. 核心：封装“导入图纸”的统一逻辑
    // ==========================================
    auto importDXFLogic = [=]() {
        QString dxfPath = QFileDialog::getOpenFileName(
            this, "导入 CAD 图纸", "", "DXF 图纸 (*.dxf);;所有文件 (*.*)"
        );

        if (dxfPath.isEmpty()) return;

        QString svgPath = QDir::tempPath() + "/kr_temp_render.svg";

        ui->textBrowser_Log->append("==============================");
        ui->textBrowser_Log->append("正在调起底座渲染引擎...");
        ui->textBrowser_Log->append("目标文件: " + dxfPath);

        QProcess process;
        QStringList args;
        
        QString scriptPath = QCoreApplication::applicationDirPath() + "/dxf2svg.py";
        args << scriptPath << dxfPath << svgPath;
        
        // 启动 Python (这里保留了你正确的路径)
        process.start("D:/miniconda3/python.exe", args);
        process.waitForFinished();
        
        QString output = process.readAllStandardOutput().trimmed();
        QString errorOutput = process.readAllStandardError().trimmed();
        
        if (output.contains("SUCCESS")) {
            cadScene->clear();
            QGraphicsSvgItem *svgItem = new QGraphicsSvgItem(svgPath);
            cadScene->addItem(svgItem);
            
            // ========================================================
            // 【关键修复 1】：必须先切页！让画板显形！
            // 因为在隐藏状态下画板尺寸是不准确的。必须先让它显示，再缩放！
            // ========================================================
            ui->stackedWidget_CAD->setCurrentIndex(1);
            QCoreApplication::processEvents(); // 强制刷新UI，获取真实的物理像素框
            
            // 【关键修复 2】：使用精准边界进行自适应
            QRectF bounds = svgItem->boundingRect();
            cadScene->setSceneRect(bounds);
            ui->graphicsView_CAD->fitInView(bounds, Qt::KeepAspectRatio);
            
            // 【视觉优化】：稍微缩小 5%，给图纸四周留出舒适的“呼吸边距”，不至于顶着边框
            ui->graphicsView_CAD->scale(0.95, 0.95); 
            
            ui->textBrowser_Log->append("<font color='#6AAB73'>图纸渲染成功！(SVG 无损模式)</font>");
        } else {
            QMessageBox::critical(this, "渲染失败", "底座引擎返回错误:\n" + errorOutput + "\n" + output);
            ui->textBrowser_Log->append("<font color='#FF5252'>图纸渲染失败！请检查 Python 环境。</font>");
        }
    }; // <---- 就是这里！之前漏掉了这个收尾的大括号和分号！

    // 绑定画板正中央的虚线大按钮
    connect(ui->btn_Big_Import, &QPushButton::clicked, this, importDXFLogic);
    
    // 绑定画板右上角重新导入的小 + 号悬浮按钮
    connect(ui->btn_Small_Import, &QPushButton::clicked, this, importDXFLogic);

    // ==========================================
    // 3. 其他按钮绑定逻辑
    // ==========================================
    connect(ui->btn_Sub_Edit, &QPushButton::clicked, this, [=]() {
        ui->textBrowser_Log->append("进入图纸编辑/修改模式...");
    });

    // 右侧大导航栏的页面切换
    connect(ui->btn_Nav_Process, &QPushButton::clicked, this, [=]() { ui->stackedWidget_Main->setCurrentIndex(0); });
    connect(ui->btn_Nav_UserParam, &QPushButton::clicked, this, [=]() { ui->stackedWidget_Main->setCurrentIndex(1); });
    connect(ui->btn_Nav_SysParam, &QPushButton::clicked, this, [=]() { ui->stackedWidget_Main->setCurrentIndex(2); });
    connect(ui->btn_Nav_CNC, &QPushButton::clicked, this, [=]() { ui->stackedWidget_Main->setCurrentIndex(3); });
}

MainWindow::~MainWindow()
{
    delete ui; 
}