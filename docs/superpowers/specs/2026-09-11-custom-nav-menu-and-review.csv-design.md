# Design: Custom-tab nav menu (Label creator / App Report) + per-app Review

Date: 2026-09-11
Status: approved

## Problem

The Custom tab's hamburger (`custom_nav_button`) currently opens a single
label-management popup. Users want a small menu with two actions: relabel an
app ("Label creator") and export an app + label CSV report. Independently, the
app detail page (full view) needs a third hamburger that opens a big Review
popup with four free-form text fields (Aesthetics, Usability, Features,
Issues), persisted per-app in the SQLite DB and included in the report.

## Decisions (from design review)

- CSV, not xlsx. No ZIP writer.
- Report covers **all** apps (`bz_state_info_get_all_entry_groups`); empty
  cells allowed.
- Category cell: all matching custom-tab pill categories joined with `"; "`.
- Report output: `<db_dir>/reports/bazaar-app-report-YYYYMMDD-HHMMSS.csv`
  where `<db_dir>` is the parent of the DB file (same parent holding
  `backups/`). Max 50 files retained (prune oldest by mtime).
- Report columns: `App Name, App ID, Category, Core Label, Custom Labels,
  Aesthetics, Usability, Features, Issues`.
- Review fields are free-form text notes (NOT numeric ratings). The core-label
  system is the non-numeric rating mechanism.
- Review hamburger on the app detail page shows a **1-item "Review" menu
  first**, then opens the big form popup (not directly to the form).
- Review data lives in a new `app_ratings` table in `custom-labels.db`.

## Implementation

### 1. bz-window.c — custom-nav popup becomes a menu

- Add `current_view` state on `BzWindow` (enum `ROOT` / `LABELS`).
- Popup window content:
  - `ROOT` view: two buttons — **"Label creator"** and **"App Report"**.
  - `LABELS` view: the existing label manager (entry + Add, name list w/ trash,
    delete-confirm), plus a **"← back"** button returning to `ROOT`.
- `rebuild_custom_nav_popup` respects `current_view`; add/delete keep the popup
  in `LABELS` view and rebuild as today.
- "App Report" handler: generate CSV via `cz_app_report_*`, prune old reports,
  toast result (AdwToastOverlay, `self->toasts`), close popup.

### 2. cz-app-report.{c,h} (new module in `custom/`, added to `bz_lib`)

- `gboolean cz_app_report_generate (BzLabelStore *store, BzStateInfo *state,
  const char *out_path, GError **err)` — builds rows for all entry groups,
  writes RFC-4180 CSV (quote fields containing `,` `"` or newline).
- `char *cz_app_report_suggest_path (const char *db_path)` — returns
  `<dirname(db_path)>/reports/bazaar-app-report-%Y%m%d-%H%M%S.csv`,
  `g_mkdir_with_parents` on the reports dir (0700).
- `void cz_app_report_prune (const char *reports_dir, guint max)` — delete
  oldest `*.csv` beyond `max` (mirror of backup retention in bz-label-store.c).
- Category cell: iterate pill-visible categories (`bz_flathub_state_get_categories`
  + `cz_category_get_show_in_list`), match apps via `cz_category_build_id_set`
  + `bz_entry_group_has_category`, display the flathub category display name.
- Core label: `bz_label_store_get_core_label`; custom labels:
  `bz_label_store_get_noncore_labels` joined `"; "`; ratings:
  `bz_label_store_get_app_review`.

### 3. bz-label-store.{c,h} — app_ratings table + API

- `CREATE TABLE IF NOT EXISTS app_ratings (app_id TEXT PRIMARY KEY NOT NULL,
  aesthetics TEXT, usability TEXT, features TEXT, issues TEXT)` added to
  `init_schema`; `user_version` bumped 1 -> 2.
- `gboolean bz_label_store_get_app_review (BzLabelStore *store, const char
  *app_id, char **aesthetics, char **usability, char **features, char **issues,
  GError **err)` — TRUE if a row exists; out params NULL when empty.
- `gboolean bz_label_store_set_app_review (BzLabelStore *store, const char
  *app_id, const char *aesthetics, const char *usability, const char *features,
  const char *issues, GError **err)` — upsert; if all four are NULL/empty,
  delete the row.
- Existing `is_empty`/`count_records`/`verify_integrity` only touch the three
  old tables — no behavior change.

### 4. bz-full-view.{blp,c} — Review hamburger

- blp: `MenuButton review_button` (open-menu icon, tooltip "Review") in the
  header end Box next to `noncore_label_button`.
- Popover: first shows a 1-item menu ("Review"); clicking it swaps the same
  popover to a big form: 4 labelled `GtkTextView`s (each in `GtkScrolledWindow`,
  ~130px tall, popover ~420px wide).
- Rebuilt on open (capture-phase gesture, matching the two label popovers);
  loaded from the current app (`bz_entry_group_get_id(self->entry_group)`).
- Auto-save: 500ms debounce after edits per field → `set_app_review`.

## GTK4 / libadwaita

All APIs already in use (gtk4 >= 4.22.1, libadwaita-1 >= 1.8); capture-phase
gesture + GtkPopover + GtkTextView patterns mirror existing full-view code.

## Tests

- `test-cz-custom-label-store.c`: `app_ratings` roundtrip, partial fields,
  all-empty deletes row, persists across reopen.
- `test-cz-app-report.c` (new meson executable): seeded harness (5 apps,
  trending+game, core/custom labels, ratings) → assert headers, rows, category
  join, quoting, package CSV bytes; `suggest_path` + reports-dir creation under
  redirected XDG_DATA_HOME; prune keeps newest 50 of 55.
- `test-core-labels.c`: `review_button` structural test — exists beside label
  buttons, popover opens, 4 labelled text views, typing persists to store.
- No `bz-window` automated test exists (heavy); custom-nav menu covered by
  manual verification steps + `just check`.

## Out of scope

- Numeric rating UI (rejected — free-form notes only).
- xlsx/zip export (rejected — CSV only).
- Threading (generation is synchronous; small datasets).
- Editing bz-window-level db/report paths beyond the above.