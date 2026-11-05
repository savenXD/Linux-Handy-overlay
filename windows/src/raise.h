// Keeps the overlay above every other window, including always-on-top ones, without activating it.
// Loop guard like src/raise.c on Linux, but gentler on Windows (60/s, burst 20, 50 ms cooldown, 100 ms poll)
// and it stops competing for a while with windows it cannot beat or that keep raising themselves.
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
