#pragma once
#include "common.h"

namespace cronq::god_copy {
// UI thread only. Text is retained in memory until replaced, locked or shutdown.
void Copy();
void Paste();
void Cleanup();
bool IsBusy();
// Internal helper entry point, before daemon/supervisor initialization.
int Worker(uintptr_t mapping);
}
