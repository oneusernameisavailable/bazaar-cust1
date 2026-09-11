# API Version Pinning

Pin these exact versions in every AI prompt when relevant.

## Runtime Dependencies

| Library | Meson Constraint | API Notes |
|---|---|---|
| GTK4 | >= 4.22.1 | GtkApplicationWindow, GtkListView, GtkEventController |
| libadwaita | >= 1.8 | AdwApplicationWindow, AdwViewStack, AdwToast |
| libdex | >= 1.0.0 | DexFuture, DexPromise, DexChannel — no raw GThread |
| Flatpak | >= 1.9 | FlatpakTransaction, FlatpakInstallation |
| AppStream | >= 1.0 | AsComponent, AsPool, AsScreenshot, AsRelease |
| libxmlb | >= 0.3.4 | XbNode, XbBuilder |
| libyaml | >= 0.2.5 | yaml_parser_t |
| libsoup | >= 3.6.0 | SoupSession |
| json-glib | >= 1.10.0 | JsonBuilder, JsonParser |
| glycin | >= 2.0 | GlycinDecoder — image loading pipeline |
| glycin-gtk4 | >= 2.0 | GlycinGtk4 — converts glycin frames to textures |
| webkitgtk | >= 2.50.2 | WebKitWebView |
| libsecret | >= 0.20 | SecretService |
| libproxy | >= 0.5 | pxProxyFactory |
| malcontent | >= 0.12.0 | MctManager — parental controls |
| gtksourceview | >= 5.17 | GtkSourceView — markdown code rendering |

## Build Dependencies

| Tool | Version |
|---|---|
| Meson | >= 1.0.0 |
| C standard | gnu11 |
| blueprint-compiler | >= 0.20.0 |
| clang-format | any |
| clang-tidy | any (optional) |

## Forbidden APIs

| API | Why |
|---|---|
| GtkWindow | Use AdwApplicationWindow |
| GThread / g_thread_new | Use libdex DexFuture |
| malloc / free | Use g_new / g_free + GObject ref-counting |
| GtkWidget signals for input | Use GtkEventController |
| GTK3 signal signatures | GTK4 changed many signatures |
| GtkListBox / GtkIconView | Use GtkListView / GtkColumnView |
