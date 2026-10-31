#include "src/dex/DexEngine.h"
#include <QCoreApplication>
#include <QDebug>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    DexEngine engine;
    const auto result = engine.selfTest();
    qInfo() << result;
    return result.value("passed").toBool() ? 0 : 1;
}
