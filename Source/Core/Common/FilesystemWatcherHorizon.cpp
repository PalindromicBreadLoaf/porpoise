// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/FilesystemWatcher.h"

// wtr::watcher has no Horizon backend, so we're going to ignore it and assume all is fine.
namespace wtr
{
inline namespace watcher
{
class watch
{
};
}  // namespace watcher
}  // namespace wtr

namespace Common
{
FilesystemWatcher::FilesystemWatcher() = default;
FilesystemWatcher::~FilesystemWatcher() = default;

void FilesystemWatcher::Watch(const std::string& path)
{
}

void FilesystemWatcher::Unwatch(const std::string& path)
{
}
}  // namespace Common
