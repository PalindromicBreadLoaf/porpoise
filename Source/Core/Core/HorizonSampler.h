// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef __SWITCH__

namespace Core
{
class System;
}

// Statistical profiler for every thread that has named itself through Common::SetCurrentThreadName.
namespace Core::HorizonSampler
{
void RegisterCpuThread();
void UnregisterCpuThread();

bool IsRunning();

void Poll(Core::System& system);

void Start(Core::System& system, double seconds);
void Stop();
void Toggle(Core::System& system, double seconds);
}  // namespace Core::HorizonSampler

#endif
