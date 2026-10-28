#include "../SettlementJournal.h"
#include "TestAcceptedQuote.h"

#include <QCoreApplication>
#include <cstdlib>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2) return 2;
    SettlementJournal journal(QString::fromLocal8Bit(argv[1]));
    const AcceptedQuote quote = TestAcceptedQuote::create(
        QString::fromLocal8Bit(argv[1]) + ".quotes", 300);
    if (!journal.ready()
        || !quote.accepted()
        || !journal.appendQuoteAccepted("crash-swap", quote, 300)) return 3;
    std::_Exit(0); // Deliberately skip destructors to model sudden process loss.
}
