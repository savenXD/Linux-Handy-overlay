// Keeps the overlay above every other window, including always-on-top ones, without activating it.
// Same loop guard as src/raise.c on Linux (token bucket 300/s, burst 60, 50 ms cooldown, 100 ms poll).
#pragma once

#include "common.h"

namespace cronq {
namespace raise {

bool Init();
void Start();   // called when the overlay is shown
void Stop();    // called when the overlay is hidden
void Cleanup();

uint64_t RaiseCount();
uint64_t TripCount();

}  // namespace raise
}  // namespace cronq
