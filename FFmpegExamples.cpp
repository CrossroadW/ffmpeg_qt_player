#include "MyQtMainWindow.h"


int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    auto *qwindow = new MyQtMainWindow(nullptr);
    qwindow->show();

    return QApplication::exec();
}
