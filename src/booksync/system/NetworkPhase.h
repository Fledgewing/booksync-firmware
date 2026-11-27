// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Admission.h"
namespace booksync {
// Runtime instance outlives every worker/token. Only main-loop service owners
// may begin/finish/acquire. UI submits Library intents; it never sees this API.
Admission& networkAdmission();
void setNetworkReading(bool reading);
bool networkDisplayReady();
void networkAdmissionTick();
}  // namespace booksync
