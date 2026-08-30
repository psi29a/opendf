#include <QApplication>

#include "MainWindow.hpp"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("OpenDF"));
    QCoreApplication::setApplicationName(QStringLiteral("OpenDF"));

    MainWindow w;
    w.show();
    return app.exec();
}
