// Kill-switch purge: removes Cronq, its startup entries and all of its data from this PC.
// Runs natively (no PowerShell) from a copy of the executable in %TEMP%, because it deletes the
// install folder it came from.
#pragma once

#include "common.h"

namespace cronq {
namespace purge {

// UI process: copies this executable to %TEMP% and starts `--purge <installDir> <ourPid>` detached.
// false if the copy could not be started (Cronq then stays running and says so).
bool Launch();

// The detached copy: waits for parentPid to exit, then removes everything. With dryRun it only
// prints what it would remove (used by tests). Never writes Cronq's log: the log lives in a
// folder this deletes.
int Run(const std::wstring& installDir, DWORD parentPid, bool dryRun);

}  // namespace purge
}  // namespace cronq
