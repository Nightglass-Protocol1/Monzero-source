#include "../BalanceReservationStore.h"
#include <QCoreApplication>
#include <cstdlib>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2) return 2;
    BalanceReservationStore store(QString::fromLocal8Bit(argv[1]));
    if (!store.ready() || !store.reserve("crash-offer", "25", "100")) return 3;
    std::_Exit(0); // Deliberately skip destructors to model sudden process loss.
}
