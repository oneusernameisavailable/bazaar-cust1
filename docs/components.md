# Bazaar Component Reference

## Core

| File | Purpose | Public API |
|---|---|---|
| `src/bz-application.c` | GApplication subclass. Async init fiber, backend orchestration, cache loading, DBus search provider, flatpak notification watcher | `bz_state_info_get_default()` |
| `src/bz-window.c` | AdwApplicationWindow. Main UI shell with AdwViewStack, pages, toast overlay | `bz_window_new()`, `bz_window_add_toast()` |
| `src/bz-application-map-factory.c` | GObject factory creating BzEntry from AppStream data | `bz_application_map_factory_new()` |
| `src/bz-env.c` | Environment variable parsing for BZ_* env vars (stack size, workers, icon size) | `bz_env_get_*()` |
| `src/bz-marshalers.list` | Custom GObject marshaller definitions | — |
| `src/main.c` | Entry point. dex_init, bz_logger_init, bge_init, create BzApplication, g_application_run | `main()` |

## Data Model

| File | Purpose | Public API |
|---|---|---|
| `src/bz-entry.c` | Core data model. Wraps AsComponent + BzFlatpakEntry + install/update state. ~2600 lines | `bz_entry_new()`, `bz_entry_get_*()`, `bz_entry_query_*()` |
| `src/bz-entry.h` | Public Entry API | BzEntry GObject type |
| `src/bz-entry-group.c` | Groups entries by category/curation | `bz_entry_group_new()`, `bz_entry_group_add_entry()` |
| `src/bz-entry-group-util.c` | Helper utilities for entry groups | — |
| `src/bz-entry-cache-manager.c` | Async disk cache. Saves/loads entries as JSON files. Batch enumeration (64 per batch) | `bz_entry_cache_manager_new()`, `bz_entry_cache_manager_lookup()`, `bz_entry_cache_manager_load()` |
| `src/bz-serializable.c` | Interface for entries that serialize/deserialize to GVariant | `bz_serializable_iface` |
| `src/bz-result.c` | Result type (Ok/Err pattern) for async operations | BzResult GObject |
| `src/bz-search-engine.c` | Full-text search with bias functions, regex boost/deboost, AppStream pool queries | `bz_search_engine_new()`, `bz_search_engine_query()` |
| `src/bz-spdx.c` | SPDX license identifier lookup and matching | `bz_spdx_lookup()` |
| `src/bz-category-flags.c` | Category flag enum and bitmask helpers | `bz_category_flags_*()` |
| `src/bz-label-store.c` | SQLite-backed label persistence (replaces `custom-labels.json`). Core per-app labels ("New"/"Install"/"4-Stars"/"3-Stars"/"Forget it"), noncore tags + assignments, and label names; shared by BzFullView and CzCustomPage. Rollback journal; flock-serialized timestamped backups in `<data>/io.github.kolunmi.Bazaar/backups` (retention 15, external copies via `BZ_PROJECT_BACKUP_DIR`); integrity-check + restore-from-backup + legacy-JSON migration on open. Stored as `<data>/io.github.kolunmi.Bazaar/custom-labels.db` | `bz_label_store_open()`, `bz_label_store_close()`, `bz_label_store_get_core_label()`, `bz_label_store_set_core_label()`, `bz_label_store_get_core_app_ids()`, `bz_label_store_add_label_name()`, `bz_label_store_get_all_label_names()`, `bz_label_store_add_noncore_label()`, `bz_label_store_remove_noncore_label()`, `bz_label_store_get_noncore_labels()`, `bz_label_store_export()` |

## Flatpak Layer

| File | Purpose | Public API |
|---|---|---|
| `src/bz-backend.c` | BzBackend GObject interface. Defines the abstraction between UI and flatpak lib | `bz_backend_*()` interface methods |
| `src/bz-flatpak-instance.c` | Wraps FlatpakInstallation. Enumerates apps, manages remotes, handles install/remove/update | `bz_flatpak_instance_*()` |
| `src/bz-flatpak-entry.c` | Wraps FlatpakInstalledRef. Ref and version management | `bz_flatpak_entry_*()` |
| `src/bz-flatpak-private.h` | Internal flatpak helpers | — |
| `src/bz-transaction.c` | BzTransaction GObject. Models a flatpak transaction (installs + updates + removals) as a tracked operation | `bz_transaction_new()`, `bz_transaction_*()` |
| `src/bz-transaction-manager.c` | Orchestrates BzTransaction lifecycle, queues operations, manages progress | `bz_transaction_manager_new()`, `bz_transaction_manager_*()` |

## Network

| File | Purpose | Public API |
|---|---|---|
| `src/bz-global-net.c` | Global SoupSession, HTTPS JSON queries to Flathub v2 API, proxy resolver | `bz_https_query_json()`, `bz_query_flathub_v2_json()` |
| `src/bz-flathub-state.c` | Flathub API v2 client. Fetches categories, collections (popular, new, updated), search results | `bz_flathub_state_new()` |
| `src/bz-flathub-category.c` | Category definitions (Adwaita, Games, Development, etc.) | — |
| `src/bz-auth-state.c` | Flathub OAuth authentication via libsecret | `bz_auth_state_*()` |
| `src/bz-favorites-page.c` | Fetches user favorites from Flathub API | — |
| `src/bz-content-provider.c` | File watching + content loading for config files | `bz_content_provider_new()` |
| `src/bz-download-worker.c` | Spawns and manages download worker subprocess. Reads stdout for JSON progress | `bz_download_worker_*()` |

## UI Components

| File | Purpose | Accessible Name Convention |
|---|---|---|
| `src/bz-all-apps-page.c` | Full app list page with GtkListView | "all-apps-page" |
| `src/bz-apps-page.c` | Apps page container | "apps-page" |
| `src/bz-library-page.c` | Installed apps library | "library-page" |
| `src/bz-search-page.c` | Search results with GtkListView | "search-page" |
| `src/bz-curated-view.c` | Curated home page with carousel + category tiles | "curated-page" |
| `src/bz-flathub-page.c` | Flathub categories browser | "flathub-page" |
| `src/bz-favorites-page.c` | User favorites list | "favorites-page" |
| `src/bz-full-view.c` | App detail view (screenshots, description, install controls) | "app-full-view" |
| `src/bz-featured-carousel.c` | AdwCarousel for featured apps | — |
| `src/bz-screenshots-carousel.c` | Screenshot carousel with controls | — |
| `src/bz-install-controls.c` | Install/remove/update button row + progress bar | — |
| `src/bz-app-tile.c` | Standard app list tile | "app-tile" |
| `src/bz-category-tile.c` | Category/collection tile | "category-tile" |
| `src/bz-featured-tile.c` | Featured app banner tile | "featured-tile" |
| `src/bz-rich-app-tile.c` | Rich app tile with description + screenshots | "rich-app-tile" |
| `src/bz-installed-tile.c` | Installed app tile with version info | "installed-tile" |
| `src/bz-favorites-tile.c` | Favorites list tile | "favorites-tile" |
| `src/bz-lazy-wdgt.c` | Lazy-loading widget container | — |
| `src/bz-context-tile.c` | Context-aware tile | — |
| `src/bz-context-row.c` | One-line context widget for lists | — |
| `src/bz-addon-tile.c` | Addon/extension tile | "addon-tile" |
| `src/bz-search-filter-popover.c` | Search filter popover (categories, sort) | — |
| `src/bz-search-pill-list.c` | Active search filter pills | — |
| `src/bz-transaction-tile.c` | Transaction progress tile | "transaction-tile" |
| `src/bz-updates-card.c` | Available updates summary card | "updates-card" |

## Dialogs

| File | Purpose |
|---|---|
| `src/bz-error-dialog.c` | Error detail dialog (AdwAlertDialog) |
| `src/bz-preferences-dialog.c` | Settings/preferences |
| `src/bz-addons-dialog.c` | App addon management |
| `src/bz-age-rating-dialog.c` | Age rating info (OARS) |
| `src/bz-app-size-dialog.c` | App install size breakdown |
| `src/bz-bundle-install-dialog.c` | .flatpakref bundle install |
| `src/bz-donations-dialog.c` | Donation links + release notes |
| `src/bz-hardware-support-dialog.c` | Hardware support matrix |
| `src/bz-license-dialog.c` | License viewer |
| `src/bz-login-page.c` | Flathub login (OAuth) |
| `src/bz-releases-dialog.c` | Version history |
| `src/bz-safety-dialog.c` | Safety/verification details |
| `src/bz-screenshot-page.c` | Full-screen screenshot viewer |
| `src/bz-stats-dialog.c` | Download statistics |
| `src/bz-transaction-dialog.c` | Active transaction dialog |
| `src/bz-transaction-list-dialog.c` | Transaction history |
| `src/bz-user-data-page.c` | User data management |

## Data Display

| File | Purpose |
|---|---|
| `src/bz-data-graph.c` | Custom GTK4 widget for data charts/graphs |
| `src/bz-world-map.c` | Download heatmap world map widget |
| `src/bz-world-map-parser.c` | Country data parser for map |
| `src/bz-article.c` | Article/news reader widget |
| `src/bz-article-list-view.c` | Article list |
| `src/bz-article-tile.c` | Article tile |
| `src/bz-appstream-description-render.c` | Rich AppStream description rendering (with webkit) |
| `src/bz-metainfo-preview.c` | AppStream metainfo preview (for developers) |

## Config & Parsing

| File | Purpose |
|---|---|
| `src/bz-hooks.c` | Hook system — shell scripts executed on app lifecycle events |
| `src/bz-parser.c` | Generic parser framework |
| `src/bz-yaml-parser.c` | YAML config file loader |
| `src/bz-newline-parser.c` | Newline-delimited text parser (for blocklists) |
| `src/bz-appstream-parser.c` | AppStream XML component parser |
| `src/bz-io.c` | File I/O utilities |
| `src/bz-safety-calculator.c` | Safety/verification scoring |
| `src/bz-malcontent-service.c` | Parental controls integration (malcontent) |
| `src/bz-template-callbacks.c` | Blueprint template callback registry |

## Accessibility & Search Provider

| File | Purpose |
|---|---|
| `src/bz-gnome-shell-search-provider.c` | DBus search provider for GNOME Shell. Responds to search queries even when window closed |

## Misc

| File | Purpose |
|---|---|
| `src/bz-inspector.c` | Developer inspector tool (debug mode only) |
| `src/bz-error.c` | Toast-based error display (`bz_show_error_for_widget()`) |
| `src/bz-logger.c` | Structured JSON logger (`bz_logger_init()`, `bz_log_json()`) |
| `src/bz-progress-bar.c` | Custom animated progress bar widget |
| `src/bz-async-texture.c` | Async image loading and caching |
| `src/bz-aspect-picture.c` | GtkPicture with aspect ratio preservation |
| `src/bz-rounded-picture.c` | Rounded-corner picture widget |
| `src/bz-decorated-screenshot.c` | Decorated screenshot (phone/tablet frame) |
| `src/bz-fading-clamp.c` | Fading edge clamp for scrollable content |
| `src/bz-developer-badge.c` | Developer verification badge |
| `src/bz-lozenge.c` | Lozenge/pill shaped label widget |
| `src/bz-zoom.c` | Zoom controller widget |

## Generated Code

| File | Generated From |
|---|---|
| `bz-marshalers.c` | `bz-marshalers.list` via `glib-genmarshal` |
| `gs-shell-search-provider-generated.*` | `shell-search-provider-dbus-interfaces.xml` via `gdbus-codegen` |
| `bz-*.h`/`bz-*.c` (many) | `bz-*.txt` via `gen_gobject.sh` — never edit directly |
| Blueprint `.blp` → `.ui` | `blueprint-compiler` |
| `bz-resources.c` | `bazaar.gresource.xml` via `glib-compile-resources` |

## Tests

| File | Tests What |
|---|---|
| `tests/unit/test_entry_cache_manager.c` | Entry cache create/destroy, living-entries-zero |
| `tests/unit/test_gobject_lifecycle.c` | Entry lifecycle, is-holding, clear-object, transaction lifecycle |
| `tests/unit/test_flatpak_transaction.c` | Transaction empty-create, get-lists, hold-release, merge-nulls |
| `tests/unit/test_search_engine.c` | Search engine create/destroy, model set/get, null/empty queries |
| `tests/unit/test_appstream_parser.c` | Null component/entry guards, from-metainfo null file |
| `tests/unit/test_data_graph.c` | Data graph widget creation, labels, animation, tooltip, model |
| `tests/unit/test_serializable.c` | Serialization interface: empty, null variant, roundtrip |
| `tests/ui/test_window_widgets.c` | AT-SPI: window title, header, search button, stack |
| `tests/ui/test_dialog_flow.c` | AT-SPI: dialog names, window description, preferences, license |

## Build & Scripts

| File | Purpose |
|---|---|
| `scripts/pre-commit.sh` | Git pre-commit hook (branch guard + format + compile + test) |
| `scripts/check.sh` | Full lint + compile + test |
| `scripts/build-coverage.sh` | Coverage build with `-Db_coverage=true` |
| `scripts/build-asan.sh` | Address sanitizer build with `-Db_sanitize=address` |
| `scripts/generate-mock-data.c` | Generates 25 mock Flathub entries as JSON |
| `.clangd` | clangd LSP config with clang-tidy |
| `.editorconfig` | Editor indentation/encoding settings |
| `.gitignore` | Generated file exclusion |
| `Justfile` | `just check`, `just lint`, `just test` recipes |
| `meson_options.txt` | Build options (bge_only, hardcoded_*_path, development, etc.) |
