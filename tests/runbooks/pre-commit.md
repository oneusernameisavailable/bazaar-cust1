# Pre-Commit Gate

Before committing, verify:

1. `git diff --stat` — confirm only intended files changed
2. `git branch --show-current` — confirm NOT on main/master
3. `just check` — lint + compile + test all pass
4. `git log --oneline -3` — review recent commits for context
5. Backup rule: the entire project must be backed up before destructive changes

If any check fails: STOP. Fix. Re-run. Do not force-push, do not --no-verify.
