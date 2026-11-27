// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
namespace booksync {
void enterLocalLibrary();
void leaveLocalLibrary();
void workLocalLibrary();
void invalidateLocalCapture();
bool takeIndexedOpenRequest(std::string& path);
}  // namespace booksync
