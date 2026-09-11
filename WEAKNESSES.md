# AI Weaknesses & Past Mistakes

## Tech Stack (per component)

| Component | Key Technologies | Common AI Mistakes |
|---|---|---|
| UI Layer | GTK4, libadwaita, Blueprint, CSS | GtkWindow instead of AdwApplicationWindow; GTK3 signal signatures; GtkWidget input signals instead of GtkEventController; GtkListBox instead of GtkListView; gtk_container_add |
| Async | libdex (DexFuture, DexPromise, DexChannel) | Writing g_thread_new or raw pthreads; forgetting dex_ref before signal_connect_data; blocking in future callbacks |
| Memory | GObject ref-counting | malloc/free instead of g_new/g_free; forgetting g_clear_object; not chaining parent finalize |
| Build | Meson | Adding .c files without updating meson.build; wrong dependency names |
| Flatpak | libflatpak, Flathub API | Calling flatpak lib directly from UI code instead of through bz-backend layer |
| Data | AppStream, json-glib, libxmlb, libyaml | Wrong AsComponent API for GTK4; json_node lifecycle (must ref/unref) |
| Backend | BzBackend interface | Tight coupling UI to backend; blocking main thread |
| Search | bz-search-engine, AppStream pool | Thread-unsafe AsPool access |
| Accessibility | AT-SPI, GTK_A11Y=test | Missing accessible-label or accessible-role; wrong accessible role names |
| Transactions | BzTransaction, FlatpakTransaction | Wrong signal signatures; not handling transaction cancel/error states |
| CSS | GTK4 CSS nodes | Targeting wrong CSS node names; overriding global theme instead of component |
| Curation | YAML config, curated sections | Wrong structure for curated config files |
| DBus | GNOME Shell Search Provider | Not keeping search provider responsive when window is closed |
| Testing | gtk_test_, AT-SPI | Forgetting GTK_A11Y=test env var; using widget variable names instead of accessible names in tests |

## Project-Specific

1. **G_LOG_DOMAIN**: Several files define `G_LOG_DOMAIN` at the top — must be defined before any includes. The logger uses this to tag JSON output.
2. **bz_lib static library**: Tests link against bz_lib, so any public API change in bz_*.c will cause test compile failures. Run `just test` after signature changes.
3. **Generated GObject files**: `.txt` files in src/ define GObject types via gen_gobject.sh. Changing these regenerates .c/.h — never edit the generated files directly.
4. **Blueprint files**: `.blp` files are compiled to GTK UI XML. Property names in Blueprint are kebab-case, not snake_case.
5. **Internal config schema**: `internal-config-schema.xml`, `main-config-schema.xml` define the GSettings schemas — must stay in sync with default config YAML files.
6. **Cache enumeration**: `bz-entry-cache-manager.c` batches enumeration in groups of 64 (`CACHE_ENUM_BATCH_SIZE`). Tests that depend on cache state must account for async enumeration.
7. **Backend notification**: BzBackendNotification maps flatpak transaction ops to UI signals via `bz-backend-notification.txt` generated GObject. Changes to the notification model require updating this spec file.
8. **Search provider shell integration**: `bz-gnome-shell-search-provider.c` must not assume a window exists. It registers as a DBus service that can be called independently.
9. **Debug mode**: `development` meson option enables extra features. The logger writes JSON to stdout — debug mode adds verbose fields.
10. **DexFuture lifecycle**: Futures returned by bz_* functions must be awaited or cancelled. Leaking a DexFuture that references a GObject prevents garbage collection.

## General AI Vibe-Coding

1. **Model hallucinates GTK3 APIs** — especially GtkWindow, GtkListBox, GtkDialog, and container functions. Always consult docs/gtk4.md first.
2. **Forgets to append to meson.build** — most common mistake. Every new .c file needs an entry in `src/meson.build`.
3. **Writes GtkWidget signal instead of GtkEventController** — GTK4 removed input signals from widgets. Must use GtkEventController*.
4. **Writes AdwToast wrong** — `adw_toast_new()` takes a title string; detail error text goes in `g_object_set_data` as "title" and "text" for the Details button.
5. **Wrong signal signatures in g_signal_connect** — GTK4 changed many signal signatures (e.g., "response" on AdwAlertDialog passes response string, not int).
6. **Omits G_DEFINE_TYPE macro** — GObject subclasses need the macro before any static functions reference the type.
7. **Forgets to run `just check`** — pre-commit hook enforces this, but it's faster to catch issues locally.
8. **Uses `gtk_widget_destroy`** — doesn't exist in GTK4. Use `g_clear_object` or `gtk_widget_unparent`.
9. **Assumes synchronous cache access** — bz-entry-cache-manager.populate entries asynchronously. Must wait for the future to resolve.
10. **Writes blocked signal callbacks with wrong return type** — GTK4 signal callbacks return void unless specified. Boolean return is rare.
