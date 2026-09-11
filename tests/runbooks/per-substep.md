# Per-Substep Checklist (read after EVERY file write or edit)

Do not skip. Read in order.

1. **AGENTS.md recall** — Which CORE LAW applies to what you just did?
   (SCALPEL? ONE-FILE-PER-STEP? NO-PANIC? NO UNSOLICITED SUMMARIES? EDIT INTENT?)

2. **DO/DON'T check** — Did you keep every promise?
   - DID run `just check` after every edit? (yes/no — if no, will before prompting user)
   - DID scope to one file or one isolated feature?
   - DID paste exact file content?
   - DID NOT change signature without updating callers?
   - DID NOT overwrite global theme/CSS?
   - DID NOT delete error tracking or logging?
   - DID NOT add unverified dependencies?

3. **WEAKNESSES.md scan** — If the file type you edited has known AI mistakes,
   check you didn't make them.

3a. **Evaluator gate** — Does this change need subagent review?
    - Trivial (comment, typo, .gitignore, config line, .editorconfig): skip
    - Non-trivial: launch evaluator subagent with tests/runbooks/eval-prompt.md + git diff

4. **Update the todo** — mark this step done.

5. **Announce next step** — what file will you write or edit next.
