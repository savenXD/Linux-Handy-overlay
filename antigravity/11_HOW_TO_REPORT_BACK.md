# How to report back

After finishing a phase (or if you get blocked), write a report as `antigravity/REPORT_PHASE_<N>.md` and tell the owner it is ready.
Then **stop**. Do not start the next phase.

## Before writing the report, run these and paste the real output

```bash
windows/scripts/build-mingw.sh 2>&1 | tail -20          # must show no "warning:" lines
windows/scripts/wine-smoke.sh 2>&1 | tail -40            # must end with "Result: N passed, 0 failed"
git status --short
git diff --stat -- src Makefile scripts install.sh uninstall.sh tests cronq.desktop README.md assets   # must print nothing
grep -rn "TODO\|FIXME\|arrives in Phase" windows/ | head                                             # explain every hit or remove it
```

Clean up disk afterwards: `rm -rf ~/.cache/cronq-wine-test` and any build directory you created outside `windows/build*/`.

## Report template (copy exactly)

```markdown
# Phase N report

## Summary
Two or three sentences: what now works.

## Status of every task
| Task | Result | Evidence |
| --- | --- | --- |
| 2.1 ... | COMPILED / UNIT TEST PASSED / PASSED UNDER WINE / UNVERIFIED (needs Windows) / NOT DONE | command or test name |

## Files added or changed
- path — one line why

## Test results (pasted, not paraphrased)
- build output tail
- wine-smoke result line (and any FAIL lines)
- unit test outputs

## Decisions I made (each marked `// DECISION:` in code)
1. what, why, how to reverse

## Deviations from the instructions
Anything you did differently or skipped, and why. "None" if none.

## Things I could not verify (needs the owner on Windows)
Numbered list with the exact manual steps (these also go into windows/README.md).

## Risks / things most likely to break on the first Windows run
Ranked, with the log line or symptom that reveals each.

## Linux files untouched
Paste the empty `git diff --stat` output.

## Questions for the owner
Only real blockers.
```

## Honesty rules (repeat)

- Use only these result labels: **COMPILED**, **UNIT TEST PASSED**, **PASSED UNDER WINE**, **UNVERIFIED (needs Windows)**, **NOT DONE**.
- If a check failed, show the failure and what you tried. Do not hide it, do not "fix" the test to make it pass unless the test itself was wrong (then explain).
- If you changed Phase 1 code, list exactly what and why, and prove the 22 original checks still pass.
- Never claim a feature "works on Windows". The strongest honest claim is "implemented, compiled, and verified under Wine as far as Wine allows".

## When you are blocked

Write what you tried, the exact error, and the 2–3 options you see with your recommendation. Do not guess on architecture and do not touch Linux files to get unblocked.
