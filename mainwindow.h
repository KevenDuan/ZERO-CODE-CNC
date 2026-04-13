#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

// 提前声明 UI 命名空间，这是 Qt 的标准做法
QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT // 这个宏非常重要，有了它才能使用信号与槽机制

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindow *ui; // 指向由 .ui 文件自动生成的界面类指针
};

#endif // MAINWINDOW_H