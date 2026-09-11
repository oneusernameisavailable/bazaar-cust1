# Post-Edit Gate

After editing, run these in order. Stop and fix if any fail.

1. `just lint` — clang-format + clang-tidy (report which file + line on failure)
2. `meson compile -C build` — compilation check (report first error: file, line, message)
3. `just test` — run all tests (report each test as [PASS]/[FAIL] name — summary)
4. If any test FAILED, read the output. Fix exactly what failed. Re-run from step 2.
5. `just check` — full check suite

**Warning scan:**
- Check `git diff` for any deletion of `g_warning`, `g_critical`, `g_debug`, `console.log` or error-tracking lines
- Check `git diff` for any change to global theme, CSS variables, or root layout
- Check for new console warnings or deprecation notices in build output

Output format for failures:
```
[FAIL] function_name at test_file.c:LINE — description of what went wrong
```
