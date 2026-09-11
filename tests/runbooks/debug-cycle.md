# Debug Cycle Protocol (read after EVERY compile/test failure — skip nothing)

Do not make another edit until the entire protocol below is followed.

## 1. STOP
- No new code. No new ideas. No jumping to a fix.
- Read the error exactly as printed: file, line, function, message type (compiler/linker/GLib runtime/SIGABRT).

## 2. DIAGNOSE
- State a **one-sentence root cause** describing **why** the program failed, not what you intend to change.
- Format: `ROOT CAUSE: [function] at [file:line] — [reason]`
- If you cannot state this confidently, you may not edit yet. Read more surrounding code.

## 3. VERIFY GTK4/LIBADWAITA API
- If the fix involves a GTK4 or libadwaita function call, confirm it exists in the local docs:
  - `grep -l '<function-name>' /usr/share/doc/gtk4/*.html`
  - `grep -l '<function-name>' /usr/share/doc/libadwaita-1/*.html`
- If not in docs, use `mcp-server-context7` to check or ask the user.
- Format: `API CHECK: gtk_foo_bar — found at /usr/share/doc/gtk4/class.Foo.html`

## 4. STATE EDIT INTENT
- Format: `EDIT INTENT: [file] — [lines modified] — [one-sentence what]`

## 5. EVALUATOR GATE
- Was this failure triggered by a non-trivial logic change (not typo/comment)?
- If yes: launch the evaluator subagent with the diff BEFORE making the fix.
- Format: `EVALUATOR: required (reason: [brief])` or `EVALUATOR: skipped (reason: trivial)`

## 6. FIX
- Only now make the edit.
- Recompile and retest immediately after.

## 7. POST-FIX CHECK
- Did the fix work? If not, go back to step 1.
- If yes, update the anchored summary with the root cause and fix.
