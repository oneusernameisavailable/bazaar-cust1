# GTK4 + libadwaita Internal Reference

Project-specific GTK4 conventions for Bazaar.
**Version baseline: GTK4 >= 4.22.1, libadwaita >= 1.8**

## 1. Forbidden → Required Patterns

| Forbidden | Required |
|---|---|
| `GtkWindow` | `AdwApplicationWindow` |
| `GtkHeaderBar` | `AdwHeaderBar` |
| `GtkStack` / `GtkStackSwitcher` | `AdwViewStack` / `AdwViewSwitcher` |
| `GtkListBox` / `GtkIconView` | `GtkListView` / `GtkColumnView` |
| `GtkDialog` | `AdwAlertDialog` |
| `GtkComboBoxText` | `AdwComboRow` |
| `gtk_container_add` / `gtk_box_pack_start` | `gtk_box_append` / `gtk_box_prepend` |
| `gtk_menu_new` | `GtkPopoverMenu` |
| `GtkWidget` signals for input | `GtkEventController*` |
| `g_signal_connect` with GTK3 signatures | GTK4 signal signatures only |
| `malloc` / `free` | `g_new` / `g_free` + GObject ref-counting |
| `GThread` / `g_thread_new` | `DexFuture` (libdex) |

## 2. Widget Hierarchy

```
AdwApplicationWindow
├── AdwHeaderBar
│   ├── GtkMenuButton (hamburger)
│   ├── GtkToggleButton (search)
│   └── AdwViewSwitcher
├── AdwViewStack ("main_stack")
│   ├── CuratedView ("curated_page")
│   │   ├── AdwCarousel
│   │   └── GtkListView — CategoryTile
│   ├── AllAppsPage ("all_apps_page")
│   │   └── GtkListView — AppTile
│   ├── LibraryPage ("library_page")
│   │   └── GtkListView — InstalledTile
│   └── SearchPage ("search_page")
│       └── GtkListView — SearchResultTile
├── FlathubPage
│   └── GtkListView — RichAppTile
└── FullViewPane
    ├── ScreenshotsCarousel
    ├── InstallControls
    └── AppDescription
```

## 3. Signal Signatures (GTK4 only)

```c
// CORRECT GTK4 signals — these differ from GTK3:
g_signal_connect (widget, "clicked", G_CALLBACK (fn), data);
g_signal_connect (list, "activate", G_CALLBACK (fn), data);
g_signal_connect (sel, "selection-changed", G_CALLBACK (fn), data);
g_signal_connect (stack, "visible-child-changed", G_CALLBACK (fn), data);
g_signal_connect (entry, "activate", G_CALLBACK (fn), data);
g_signal_connect (search_entry, "search-changed", G_CALLBACK (fn), data);
```

## 4. Event Controllers

| GTK3 Signal | GTK4 Controller |
|---|---|
| `button-press-event` | `GtkGestureClick` — connect `"pressed"` or `"released"` |
| `key-press-event` | `GtkEventControllerKey` — connect `"key-pressed"` |
| `scroll-event` | `GtkEventControllerScroll` — connect `"scroll"` |
| `focus-in-event` / `focus-out-event` | `GtkEventControllerFocus` — connect `"enter"` / `"leave"` |
| `motion-notify-event` | `GtkEventControllerMotion` — connect `"motion"` |

```c
// Pattern — add controller, connect, forget
GtkEventController *ec = gtk_event_controller_key_new ();
g_signal_connect (ec, "key-pressed", G_CALLBACK (on_key), data);
gtk_widget_add_controller (widget, ec);
```

## 5. GtkListView / GtkColumnView

```c
GtkSelectionModel *sel = GTK_SELECTION_MODEL (gtk_single_selection_new (model));
GtkListItemFactory *factory = gtk_signal_list_item_factory_new ();
g_signal_connect (factory, "setup", G_CALLBACK (on_setup), NULL);
g_signal_connect (factory, "bind", G_CALLBACK (on_bind), NULL);
g_signal_connect (factory, "unbind", G_CALLBACK (on_unbind), NULL);
g_signal_connect (factory, "teardown", G_CALLBACK (on_teardown), NULL);
GtkWidget *lv = g_object_new (GTK_TYPE_LIST_VIEW,
    "model", sel,
    "factory", factory,
    "single-click-activate", TRUE, NULL);
```

Factory signal handlers signature:
```c
void on_setup    (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data);
void on_bind     (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data);
void on_unbind   (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data);
void on_teardown (GtkListItemFactory *factory, GtkListItem *item, gpointer user_data);
```

Access item data: `g_object obj = gtk_list_item_get_item (item);`
Access child widget: `GtkWidget *w = gtk_list_item_get_child (item);`

## 6. GObject Widget Lifecycle

```c
struct _BzFoo {
  GtkWidget parent;
  gchar *owned_str;
  GObject *owned_obj;
};

G_DEFINE_TYPE (BzFoo, bz_foo, GTK_TYPE_WIDGET)

static void
bz_foo_init (BzFoo *self) { }

static void
bz_foo_finalize (GObject *object)
{
  BzFoo *self = BZ_FOO (object);
  g_free (self->owned_str);
  g_clear_object (&self->owned_obj);
  G_OBJECT_CLASS (bz_foo_parent_class)->finalize (object);
}

static void
bz_foo_class_init (BzFooClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  oc->finalize = bz_foo_finalize;
  // NOT dispose — GTK4 uses finalize for widget cleanup
}
```

## 7. AT-SPI Accessible Properties

Every widget sets `accessible-role` and `accessible-label`:

```c
GtkWidget *btn = g_object_new (GTK_TYPE_BUTTON,
  "label", "Search",
  "accessible-label", "Search all applications",
  NULL);
```

Roles used in Bazaar: `push-button`, `tab`, `tab-list`, `list`, `list-item`, `header`, `search`, `dialog`, `banner`, `carousel`, `text-input`.

Test lookup: `gtk_test_find_widget (parent, "accessible-label-text")`

## 8. CSS Node Names

```
window, window.background
headerbar, stack, listview, listview > row
button, button.suggested-action, button.destructive-action
scrolledwindow, box, label, entry, searchbar
adwheaderbar, adwviewstack, adwviewswitcher
adwcarousel, adwclamp, adwtoast
adwalertdialog, adwcomborow, adwentryrow
```

## 9. Blueprint Conventions

- Widget names = C type identifiers: `BzAppTile { }`
- Properties in kebab-case: `margin-top: 12;`
- Signal handlers: `Clicked => on_clicked();`
- Children in braces, not via properties
- Accessible names match visible labels

## 10. Testing

```c
// env: GTK_A11Y=test
gtk_test_init (&argc, &argv, NULL);
GtkWidget *w = gtk_test_find_widget (parent, "accessible-label-here");
g_assert_nonnull (w);
```

## 11. Error Handling

Functions that can fail return `gboolean` + `GError**`. Call pattern:
```c
g_autoptr(GError) error = NULL;
if (!bz_something (param1, param2, &error))
  bz_show_error_for_widget (error, widget);
```

## 12. Backend Decoupling

- No direct Flatpak lib calls in UI code (`src/` without `flatpak/` path)
- UI → `BzBackend` interface → `src/flatpak/` layer
- Same for AppStream, malcontent, glycin, webkit
