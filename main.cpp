
/**
    @file main.cpp
    @breif Where the magic starts
    @author Natesh Narain
*/

#include "mainwindow.h"
#include <QApplication>

#include "messagequeue.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}
