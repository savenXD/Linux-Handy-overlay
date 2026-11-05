// `cronq update`: manual update check. Cronq never contacts the network on its own; this runs only when
// the user types the command. Downloads are accepted only when their SHA-256 matches the release's
// SHA256SUMS.txt.
#pragma once

#include "common.h"

namespace cronq {
namespace update {

int Run(bool assumeYes);  // console: prints progress; 0 = up to date or installer started

}  // namespace update
}  // namespace cronq
