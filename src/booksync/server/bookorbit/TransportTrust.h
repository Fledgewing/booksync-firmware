// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace booksync::server {
// Public ISRG roots retained from the existing transport trust policy. Borrowed
// flash storage, never a promise to accept an unknown/self-signed certificate.
const char* defaultTransportRoots();
}  // namespace booksync::server
