# Bazaar Business Rules & Edge Cases

Documented for AI prompts. If a prompt touches these areas, read this file first.

## Service Mode

- Bazaar runs as a background service (DBus activation). Closing all windows does not exit the process.
- The gnome-shell search provider DBus interface must remain responsive even with no visible window.
- `bz-gnome-shell-search-provider.c` handles this.

## Backend Decoupling

- All UI code is decoupled from backend operations. The backend runs on its own thread via libdex.
- `BzBackend` is an interface. Currently only `BzFlatpakInstance` implements it.
- UI calls `bz_backend_*` which return `DexFuture*`. The UI never calls libflatpak directly.
- Breaking this decoupling is a hard violation.

## Multi-Threaded Data Access

- `BzEntryCacheManager` is the single source of truth for entry lifecycle.
- `BzEntryGroup` has a `GMutexLocker` for thread-safe access.
- UI reads are on the main thread. Backend writes are on worker threads.
- Writes to shared state must go through `bz_entry_group_lock()`.
- `bz_data_graph.c` must not assume it owns its data.

## Flatpak Transaction Model

- One install = one `BzTransaction` with installs/updates/removals arrays.
- Transactions can be merged via `bz_transaction_new_merged()`.
- `BzTransactionManager` serializes execution (one at a time).
- `bz_transaction_manager_add()` returns a `DexFuture*` that resolves when the transaction finishes.
- Cancellation: `bz_transaction_manager_cancel_current()`.
- Transactions can be held (`bz_transaction_hold()`) to prevent garbage collection while async ops are in flight.

## Curation System (YAML)

- Distributors configure Bazaar via YAML files.
- `bz-yaml-parser.c` parses the YAML into `BzMainConfig` and `BzBlocklist`.
- The curated tab, featured apps, banners, articles are all driven by YAML config.
- If YAML is missing or malformed, Bazaar falls back to a default view (all apps, alphabetical).
- The YAML config path can be hardcoded at build time via `HARDCODED_MAIN_CONFIG`.

## Blocklist

- Blocklist is a compiled GVariant file (not raw YAML at runtime).
- Blocklist entries have conditions (`BzBlocklistCondition`) that can match by:
  - Environment variable (`BZ_BLOCKLIST_CONDITION_MATCH_ENVVAR`)
  - Locale (`BZ_BLOCKLIST_CONDITION_MATCH_LOCALE`)
- Matched entries are hidden from all views.

## Image Loading (Glycin)

- All app screenshots and icons go through `GlycinDecoder`.
- `bz-async-texture.c` and `bz-aspect-picture.c` handle async decode + display.
- Glycin runs in a sandboxed subprocess. If glycin crashes, the image slot remains blank (not a crash).
- Icons are cached by `unique_id_checksum` in the filesystem cache.

## Malcontent (Parental Controls)

- `bz-malcontent-service.c` checks if the user has parental controls via MctManager.
- Overage apps are filtered out of all lists.
- If malcontent is unavailable (not installed), all apps are shown.
- Edge case: malcontent service may be blocked by the flatpak sandbox.

## Error Display

- `bz_show_error_for_widget()` shows a transient toast with a "Details" button.
- The toast is `AdwToast` added to the nearest `AdwToastOverlay`.
- Persistent/blocking errors use `BzErrorDialog` (modal).

## Search

- Search is local (no network). Results are filtered from the loaded `GListModel`.
- `BzSearchEngine` queries against indexed fields: title, developer, description, search_tokens.
- Search biases (from YAML) can boost certain app results.

## Age Ratings

- Age ratings come from AppStream metadata (`AsContentRating`).
- `bz-age-rating-dialog.c` shows when an app exceeds the user's configured threshold.
- The threshold is saved in GSettings.

## Donations / Flathub Auth

- Donation links are per-app from AppStream metadata.
- Flathub account auth is stored via `libsecret`.
- `bz-auth-state.c` manages the OAuth-like flow.

## Known Edge Cases

1. **Empty Flathub remote**: If no remotes are configured, Bazaar shows an empty state with a "Add Flathub" button.
2. **Network offline during load**: Entries from cache are shown with a banner indicating offline. `bz-global-net.c` monitors connectivity.
3. **Concurrent flatpak operations**: The `BzTransactionManager` queues transactions — UI shows a progress queue.
4. **Addon relationships**: `BzEntryGroup` tracks parent-child between apps and addons. Addons cannot be installed without their parent.
5. **Bundle files**: Installing from `.flatpak` bundles goes through `bz-bundle-install-dialog.c` — different code path than remote installs.
6. **EOL apps**: Apps marked end-of-life in AppStream show a warning. Configurable via `override_eol_markings` in YAML.
7. **FOSS filter**: `bz_entry_get_is_foss()` uses AppStream metadata. Not all apps declare this correctly.
