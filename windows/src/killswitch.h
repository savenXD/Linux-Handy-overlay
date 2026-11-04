// Alt+Shift+K kill switch: an on-screen confirmation, then a detached cleanup script that removes
// Cronq and all of its data from this PC.
#pragma once

#include "common.h"

namespace cronq {
namespace killswitch {

void ShowPrompt();  // UI thread: show the Enter/Esc confirmation box (auto-cancels after 15 s)
void Dismiss();     // UI thread: close the box without doing anything
void Confirm();     // UI thread: close the box, start the cleanup script and quit
bool IsPending();

}  // namespace killswitch
}  // namespace cronq
