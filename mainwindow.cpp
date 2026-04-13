#include "mainwindow.h"
#include "ui_modern_cnc_dark.h" 
#include <QFileDialog>
#include <QMessageBox>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QProcess>            // 用于在后台静默调用 Python 脚本
#include <QGraphicsSvgItem>    // 用于完美渲染 SVG 矢量图
#include <QDir>
#include <QCoreApplication>
#include <QWheelEvent>         // 【必须有】鼠标滚轮事件
#include <QEvent>              // 【必须有】基础事件头文件

// ==========================================
// 【你之前漏掉的核心】：康瑞 CAD 视口滚轮缩放拦截器
// ==========================================
class CADZoomFilter : public QObject {
public:
    CADZoomFilter(QGraphicsView* view) : QObject(view), mView(view) {}
protected:
    bool eventFilter(QObject *obj, QEvent *event) override {
        // 拦截滚轮事件
        if (event->type() == QEvent::Wheel) {
            QWheelEvent *wheelEvent = static_cast<QWheelEvent*>(event);
            if (wheelEvent->angleDelta().y() > 0) {
                // 向上滚：放大 15%
                mView->scale(1.15, 1.15); 
            } else {
                // 向下滚：缩小
                mView->scale(1.0 / 1.15, 1.0 / 1.15); 
            }
            return true; // 告诉系统事件已处理
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

    // ==========================================
    // 1. 初始化 CAD 绘图引擎 (SVG 矢量渲染模式)
    // ==========================================
    QGraphicsScene *cadScene = new QGraphicsScene(this);
    ui->graphicsView_CAD->setScene(cadScene);
    
    // ==========================================
    // 【你之前漏掉的核心】：开启工业级鼠标交互体验
    // ==========================================
    // 1. 开启左键拖拽平移图纸功能 (会出现抓手)
    ui->graphicsView_CAD->setDragMode(QGraphicsView::ScrollHandDrag);
    // 2. 开启以鼠标指针为中心的智能缩放 (指哪放大哪)
    ui->graphicsView_CAD->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    // 3. 将我们写的滚轮拦截器装在画板的视口上
    ui->graphicsView_CAD->viewport()->installEventFilter(new CADZoomFilter(ui->graphicsView_CAD));
    // ==========================================

    // 初始化提示文字
    QGraphicsTextItem *textItem = cadScene->addText("康瑞渲染引擎已就绪\n请点击上方“文件”导入图纸...", QFont("Microsoft YaHei", 14));
    textItem->setDefaultTextColor(QColor("#5C616C"));

    // ==========================================
    // 2. 绑定“文件”按钮：调用 Python 外挂进行无损解析
    // ==========================================
    connect(ui->btn_Sub_File, &QPushButton::clicked, this, [=]() {
        // 弹出文件选择器，限制选择 .dxf 文件
        QString dxfPath = QFileDialog::getOpenFileName(
            this, "导入 CAD 图纸", "", "DXF 图纸 (*.dxf);;所有文件 (*.*)"
        );

        if (dxfPath.isEmpty()) return;

        // 确定输出的临时 SVG 文件路径 (存放在系统临时文件夹中)
        QString svgPath = QDir::tempPath() + "/kr_temp_render.svg";

        // 在右下角日志框中提示用户
        ui->textBrowser_Log->append("正在调起底座渲染引擎...");
        ui->textBrowser_Log->append("目标文件: " + dxfPath);

        // 使用 QProcess 在后台静默调用 dxf2svg.py 脚本
        QProcess process;
        QStringList args;
        
        QString scriptPath = QCoreApplication::applicationDirPath() + "/dxf2svg.py";
        args << scriptPath << dxfPath << svgPath;
        
        // 【保留了你正确的 Python 路径】
        process.start("D:/miniconda3/python.exe", args);
        process.waitForFinished();
        
        // 读取 Python 脚本的输出结果
        QString output = process.readAllStandardOutput().trimmed();
        QString errorOutput = process.readAllStandardError().trimmed();
        
        if (output.contains("SUCCESS")) {
            // 转换成功！清空画板
            cadScene->clear();
            
            // 使用 Qt 原生引擎加载生成的 SVG 矢量图
            QGraphicsSvgItem *svgItem = new QGraphicsSvgItem(svgPath);
            cadScene->addItem(svgItem);
            
            // 完美居中并自适应黑框大小
            ui->graphicsView_CAD->fitInView(svgItem, Qt::KeepAspectRatio);
            
            ui->textBrowser_Log->append("<font color='#67996e'>图纸渲染成功！(SVG 无损模式)</font>");
        } else {
            // 转换失败，弹出错误提示
            QMessageBox::critical(this, "渲染失败", "底座引擎返回错误:\n" + errorOutput + "\n" + output);
            ui->textBrowser_Log->append("<font color='#FF5252'>图纸渲染失败！请检查 Python 环境。</font>");
        }
    });

    // ==========================================
    // 3. 绑定左侧导航栏的页面切换逻辑
    // ==========================================
    connect(ui->btn_Nav_Process, &QPushButton::clicked, this, [=]() {
        ui->stackedWidget_Main->setCurrentIndex(0);
    });
    connect(ui->btn_Nav_UserParam, &QPushButton::clicked, this, [=]() {
        ui->stackedWidget_Main->setCurrentIndex(1);
    });
    connect(ui->btn_Nav_SysParam, &QPushButton::clicked, this, [=]() {
        ui->stackedWidget_Main->setCurrentIndex(2);
    });
    connect(ui->btn_Nav_CNC, &QPushButton::clicked, this, [=]() {
        ui->stackedWidget_Main->setCurrentIndex(3);
    });
}

MainWindow::~MainWindow()
{
    delete ui;
}