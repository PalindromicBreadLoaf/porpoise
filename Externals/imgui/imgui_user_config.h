#pragma once

#include "Common/Assert.h"

#define IM_ASSERT(_EXPR) ASSERT(_EXPR)

#define IMGUI_DISABLE_DEMO_WINDOWS

#ifdef __SWITCH__
// Horizon has no fork/exec and nothing to hand a path off to.
#define IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS
#endif
