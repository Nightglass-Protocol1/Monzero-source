#ifndef MONZERO_BCH_PRIVATE_SUCCESS_PROCESS_H
#define MONZERO_BCH_PRIVATE_SUCCESS_PROCESS_H
#include "BchPrivateSuccessProtocol.h"
#include <atomic>
namespace BchPrivateSuccessProcess {
// Blocking private pipe exchange; call from a worker, never the GUI thread.
// Expected helper digest and configuration must be independently trusted.
// This does not authorize an operation or replace public review validation.
// Input is erased on every outcome. Result is populated only after successful
// exit and strict operation/request/trade validation. No diagnostics escape.
bool exchange(const QString &program,const QString &digest,const QString &configuration,
    BchPrivateSuccessProtocol::Action action,const QString &requestId,const QString &tradeId,
    BchPrivateSuccessProtocol::Request &input,BchPrivateSuccessProtocol::Material &result,
    int timeoutMs=180000,const std::atomic_bool *cancelled=nullptr);
}
#endif
