#include <QApplication>
#include "MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("neil289506"));
    QCoreApplication::setApplicationName(QStringLiteral("VcpkgCompileTool"));
    MainWindow w;
    w.show();
    return app.exec();
}
