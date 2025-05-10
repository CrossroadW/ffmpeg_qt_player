#include <QApplication>
#include "MainWindow.h"
#include <spdlog/spdlog.h>

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
     MainWindow  w{};
    w.show();
    // default log level print
    auto level = spdlog::get_level();
    spdlog::info("level: {}", static_cast<int>(level));

    return QApplication::exec();
}
