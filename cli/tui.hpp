#pragma once

#include "source.hpp"

namespace tui {

// Full-screen interactive search over `source`. Returns when the user quits
// (after the source was closed, which saves a local index).
int run(IndexSource& source, wchar_t drive);

} // namespace tui
