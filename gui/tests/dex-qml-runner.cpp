#include <QtQuickTest/quicktest.h>
#include <QResource>
#include <QByteArray>

// Test-only runner: loads the application's resource bundle without constructing
// its wallet manager, starting a daemon, or reading application wallet settings.
int main(int argc, char **argv) {
    const auto bundle=qgetenv("MONZERO_TEST_RESOURCE_BUNDLE");
    if (bundle.isEmpty() || !QResource::registerResource(QString::fromLocal8Bit(bundle))) return 2;
    return quick_test_main(argc,argv,"MonzeroDexPresentation",nullptr);
}
