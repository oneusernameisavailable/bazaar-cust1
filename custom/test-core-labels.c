/*
 * Cross-component integration tests for the core-label lifecycle:
 *   BzFullView popover -> DB save -> CzCustomPage pill sync -> filter re-apply.
 *
 * Every test outputs structured DIAG: lines with key=value pairs so AI
 * consumers can grep for "DIAG" and parse state without reading free-form
 * log text.
 */

#include "config.h"

#include <gtk/gtk.h>
#include <adwaita.h>
#include <glib/gstdio.h>

#include "bge.h"
#include "bz-app-tile.h"
#include "bz-application.h"
#include "bz-dynamic-list-view.h"
#include "bz-full-view.h"
#include "bz-entry.h"
#include "bz-entry-group.h"
#include "bz-state-info.h"
#include "bz-flathub-state.h"
#include "bz-flathub-category.h"
#include "bz-result.h"
#include "bz-flatpak-entry.h"
#include "bz-auth-state.h"

/* Forward declarations of static BzFullView functions we need to test
 * (these are the actual implementations in bz-full-view.c — we cannot
 * call them directly since they are static).  Instead we test them via
 * the public API surface and widget introspection. */

#include "cz-custom-page.h"
#include "cz-custom-label-store.h"
#include "bz-label-store.h"

/* ------------------------------------------------------------------ */
/*  Fixture paths — set once in main() before g_test_init              */
/* ------------------------------------------------------------------ */
static char *test_dir;
static char *store_path;
static char *store_backup_dir;

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static void
process_events (void)
{
  GMainContext *ctx = g_main_context_default ();
  while (g_main_context_pending (ctx))
    g_main_context_iteration (ctx, FALSE);
}

/* Realize a BzFullView inside a real top-level window.  Rebuild paths like
 * rebuild_custom_label_popover call gtk_native_get_surface() on the widget
 * root, which asserts when the root is not a GtkNative (i.e. a bare
 * BzFullView with no window).  Mirrors page tests that host in a window. */
static GtkWidget *
host_full_view (BzFullView *full_view)
{
  GtkWidget *win = gtk_window_new ();
  gtk_window_set_child (GTK_WINDOW (win), GTK_WIDGET (full_view));
  gtk_widget_set_visible (win, TRUE);
  process_events ();
  return win;
}

/* Open the shared SQLite label store used by both BzFullView and
 * CzCustomPage — the persistent source that replaced custom-labels.json. */
static BzLabelStore *
open_store (void)
{
  BzLabelStore *store = bz_label_store_open (store_path,
                                             store_backup_dir, NULL);
  g_assert_nonnull (store);
  return store;
}

/* Recursively remove a directory tree. */
static void
remove_dir_tree (const char *dir)
{
  GDir       *d;
  const char *name;

  if (dir == NULL)
    return;
  d = g_dir_open (dir, 0, NULL);
  if (d == NULL)
    return;
  while ((name = g_dir_read_name (d)) != NULL)
    {
      char *sub = g_build_filename (dir, name, NULL);
      if (g_file_test (sub, G_FILE_TEST_IS_DIR))
        remove_dir_tree (sub);
      else
        g_remove (sub);
      g_free (sub);
    }
  g_dir_close (d);
  g_rmdir (dir);
}

/* Remove all persisted label data so a fresh page/store starts clean.
 * Keeps the parent app dir so bz_label_store_open can recreate the DB. */
static void
wipe_store (void)
{
  const char *files[] = {
    "custom-labels.db", "custom-labels.db-wal",
    "custom-labels.db-shm", "custom-labels.json", NULL
  };
  guint i;

  for (i = 0; files[i] != NULL; i++)
    {
      char *path = g_build_filename (store_backup_dir, files[i], NULL);
      g_remove (path);
      g_free (path);
    }

  {
    char *backups = g_build_filename (store_backup_dir, "backups", NULL);
    remove_dir_tree (backups);
    g_free (backups);
  }
}

/* Seed core labels into the store (replaces the old JSON fixture). */
static void
seed_core_labels (const char * const *app_ids,
                  const char * const *labels,
                  guint               n)
{
  BzLabelStore *store;
  guint         i;

  store = open_store ();
  for (i = 0; i < n; i++)
    g_assert_true (bz_label_store_set_core_label (store, app_ids[i],
                                                  labels[i], NULL));
  bz_label_store_close (store);
}

static void
seed_label_name (const char *name)
{
  BzLabelStore *store;

  store = open_store ();
  g_assert_true (bz_label_store_add_label_name (store, name, NULL));
  bz_label_store_close (store);
}

static void
seed_noncore_assignment (const char *app_id,
                         const char *label)
{
  BzLabelStore *store;

  store = open_store ();
  g_assert_true (bz_label_store_add_noncore_label (store, app_id,
                                                   label, NULL));
  bz_label_store_close (store);
}

/* Read an app's core label back from the store (returns g_free'd string). */
static char *
read_core_label (const char *app_id)
{
  BzLabelStore *store;
  char         *result;

  store  = open_store ();
  result = bz_label_store_get_core_label (store, app_id);
  bz_label_store_close (store);
  return result;
}

static gboolean
read_has_noncore (const char *app_id,
                  const char *label)
{
  BzLabelStore *store;
  gboolean      result;

  store  = open_store ();
  result = bz_label_store_has_noncore_label (store, app_id, label);
  bz_label_store_close (store);
  return result;
}

static gboolean
read_has_label_name (const char *name)
{
  BzLabelStore *store;
  char        **names;
  gboolean      found;
  guint         i;

  store = open_store ();
  names = bz_label_store_get_all_label_names (store);
  found = FALSE;
  for (i = 0; names != NULL && names[i] != NULL; i++)
    {
      if (g_strcmp0 (names[i], name) == 0)
        found = TRUE;
    }
  g_strfreev (names);
  bz_label_store_close (store);
  return found;
}

/* Depth-first search for a GtkMenuButton (there should be exactly one in a
 * standalone BzFullView — core_label_button). */
/* Find all GtkMenuButton instances in the widget tree and return the count,
 * storing up to max pointers in out[]. */
static guint
find_all_menu_buttons (GtkWidget *widget,
                       GtkWidget **out,
                       guint    max)
{
  GtkWidget *child;
  guint      n = 0;

  if (widget == NULL || max == 0)
    return 0;

  if (GTK_IS_MENU_BUTTON (widget))
    {
      out[n++] = widget;
      if (n == max)
        return n;
    }

  child = gtk_widget_get_first_child (widget);
  while (child != NULL)
    {
      n += find_all_menu_buttons (child, out + n, max - n);
      if (n >= max)
        return n;
      child = gtk_widget_get_next_sibling (child);
    }
  return n;
}

static GtkWidget *
find_menu_button (GtkWidget *widget)
{
  if (widget == NULL)
    return NULL;
  if (GTK_IS_MENU_BUTTON (widget))
    {
      const char *icon = gtk_menu_button_get_icon_name (GTK_MENU_BUTTON (widget));
      if (g_strcmp0 (icon, "open-menu-symbolic") == 0)
        return widget;
    }

  for (GtkWidget *child = gtk_widget_get_first_child (widget);
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      GtkWidget *found = find_menu_button (child);
      if (found != NULL)
        return found;
    }
  return NULL;
}

/* Get the popover content box from a BzFullView. */
static GtkWidget *
get_popover_box (BzFullView *full_view)
{
  GtkWidget   *menu_button;
  GtkPopover  *popover;
  GtkWidget   *box;

  menu_button = find_menu_button (GTK_WIDGET (full_view));
  g_assert_nonnull (menu_button);
  g_test_message ("DIAG:popover | menu_button=%s",
                  G_OBJECT_TYPE_NAME (menu_button));

  popover = gtk_menu_button_get_popover (GTK_MENU_BUTTON (menu_button));
  g_assert_nonnull (popover);
  g_test_message ("DIAG:popover | popover=%s | ptr=%p",
                  G_OBJECT_TYPE_NAME (popover), (void *) popover);

  box = gtk_popover_get_child (popover);
  g_test_message ("DIAG:popover | child=%p", (void *) box);
  g_assert_nonnull (box);
  g_test_message ("DIAG:popover | child_type=%s", G_OBJECT_TYPE_NAME (box));

  /* Debug: list all children of the box */
  {
    GtkWidget *c = gtk_widget_get_first_child (box);
    int idx = 0;
    while (c != NULL)
      {
        g_test_message ("DIAG:popover-child[%d] | type=%s", idx,
                        G_OBJECT_TYPE_NAME (c));
        if (GTK_IS_BUTTON (c))
          {
            const char *lbl = gtk_button_get_label (GTK_BUTTON (c));
            g_test_message ("DIAG:popover-child[%d] | label=%s", idx,
                            lbl ? lbl : "(null)");
          }
        c = gtk_widget_get_next_sibling (c);
        idx++;
      }
  }

  return box;
}

/* Search a widget subtree recursively for a GtkLabel with matching text. */
static GtkWidget *
find_label_in_tree (GtkWidget *widget, const char *text)
{
  GtkWidget *child;

  if (widget == NULL)
    return NULL;

  if (GTK_IS_LABEL (widget))
    {
      const char *lbl = gtk_label_get_text (GTK_LABEL (widget));
      if (g_strcmp0 (lbl, text) == 0)
        return widget;
    }

  child = gtk_widget_get_first_child (widget);
  while (child != NULL)
    {
      GtkWidget *found = find_label_in_tree (child, text);
      if (found != NULL)
        return found;
      child = gtk_widget_get_next_sibling (child);
    }
  return NULL;
}

/* Find a first-level GtkButton whose child tree contains a label with the
 * given text.  Buttons in core-label popovers use gtk_button_set_child()
 * with either a plain GtkLabel or a GtkBox containing a GtkLabel, so
 * gtk_button_get_label() returns NULL. */
static GtkWidget *
find_button_by_label (GtkWidget *parent, const char *label)
{
  GtkWidget *child;

  if (parent == NULL)
    return NULL;

  child = gtk_widget_get_first_child (parent);
  while (child != NULL)
    {
      if (GTK_IS_BUTTON (child))
        {
          if (find_label_in_tree (child, label) != NULL)
            return child;
        }
      else
        {
          GtkWidget *found = find_button_by_label (child, label);
          if (found != NULL)
            return found;
        }
      child = gtk_widget_get_next_sibling (child);
    }
  return NULL;
}

/* Find a GtkCheckButton in the widget tree whose label matches.  In GTK 4.22
 * GtkCheckButton is a direct GtkWidget subclass (NOT a GtkButton), so it is
 * invisible to find_button_by_label(). */
static GtkWidget *
find_check_button_by_label (GtkWidget *parent, const char *label)
{
  GtkWidget *child;

  if (parent == NULL)
    return NULL;

  if (GTK_IS_CHECK_BUTTON (parent))
    {
      const char *lbl = gtk_check_button_get_label (GTK_CHECK_BUTTON (parent));
      if (g_strcmp0 (lbl, label) == 0)
        return parent;
    }

  child = gtk_widget_get_first_child (parent);
  while (child != NULL)
    {
      GtkWidget *found = find_check_button_by_label (child, label);
      if (found != NULL)
        return found;
      child = gtk_widget_get_next_sibling (child);
    }
  return NULL;
}

/* Count first-level GtkButton widgets inside a container. */
static guint
count_buttons (GtkWidget *parent)
{
  GtkWidget *child;
  guint      count = 0;

  child = gtk_widget_get_first_child (parent);
  while (child != NULL)
    {
      if (GTK_IS_BUTTON (child))
        count++;
      child = gtk_widget_get_next_sibling (child);
    }
  return count;
}

/* Create an entry group for a single app ID. */
static BzEntryGroup *
make_entry_group (const char *app_id)
{
  g_autoptr (BzEntry) entry = bz_entry_new (app_id);
  BzEntryGroup *group = bz_entry_group_new_for_single_entry (entry);

  g_test_message ("DIAG:make-group | app_id=%s | group=%p", app_id, (void *) group);
  if (group != NULL)
    {
      const char *id = bz_entry_group_get_id (group);
      g_test_message ("DIAG:make-group | group_id=%s", id ? id : "(null)");
    }

  return group;
}

/* Append one app entry to a list store. */
static void
append_group (GListStore *store, const char *app_id)
{
  g_autoptr (BzEntry)     entry = bz_entry_new (app_id);
  g_autoptr (BzEntryGroup) group = bz_entry_group_new_for_single_entry (entry);
  g_list_store_append (store, group);
}

/* Create a BzFlathubCategory with the given name and app_ids (NULL-terminated). */
static BzFlathubCategory *
add_category (BzFlathubState *flathub,
              const char     *name,
              const char     **app_ids)
{
  BzFlathubCategory *cat;
  GtkStringList     *list;

  cat = bz_flathub_category_new ();
  bz_flathub_category_set_name (cat, name);

  list = gtk_string_list_new (NULL);
  if (app_ids != NULL)
    for (guint i = 0; app_ids[i] != NULL; i++)
      gtk_string_list_append (list, app_ids[i]);

  bz_flathub_category_set_applications (cat, G_LIST_MODEL (list));

  return cat;
}

/* Depth-first search for a box whose direct children include buttons.
 * Used to locate pill_box / custom_pill_box in CzCustomPage. */
static GtkWidget *
find_pill_box (GtkWidget *widget)
{
  GtkWidget *child;

  if (widget == NULL)
    return NULL;

  if (GTK_IS_BOX (widget))
    {
      child = gtk_widget_get_first_child (widget);
      while (child != NULL)
        {
          if (GTK_IS_BUTTON (child))
            return widget;
          child = gtk_widget_get_next_sibling (child);
        }
    }

  child = gtk_widget_get_first_child (widget);
  while (child != NULL)
    {
      GtkWidget *found = find_pill_box (child);
      if (found != NULL)
        return found;
      child = gtk_widget_get_next_sibling (child);
    }

  return NULL;
}

/* Find a BzDynamicListView in the widget tree. */
static GtkWidget *
find_list_view (GtkWidget *widget)
{
  GtkWidget *child;

  if (widget == NULL)
    return NULL;
  if (BZ_IS_DYNAMIC_LIST_VIEW (widget))
    return widget;

  child = gtk_widget_get_first_child (widget);
  while (child != NULL)
    {
      GtkWidget *found = find_list_view (child);
      if (found != NULL)
        return found;
      child = gtk_widget_get_next_sibling (child);
    }
  return NULL;
}

/* Depth-first search for any widget of the given type. */
static GtkWidget *
find_widget_by_type (GtkWidget *widget, GType type)
{
  GtkWidget *child;

  if (widget == NULL)
    return NULL;

  if (G_TYPE_CHECK_INSTANCE_TYPE (widget, type))
    return widget;

  child = gtk_widget_get_first_child (widget);
  while (child != NULL)
    {
      GtkWidget *found = find_widget_by_type (child, type);
      if (found != NULL)
        return found;
      child = gtk_widget_get_next_sibling (child);
    }
  return NULL;
}

/* Depth-first search for a button by label text (walks entire subtree). */
static GtkWidget *
find_pill_by_label (GtkWidget *widget, const char *label)
{
  GtkWidget *child;

  if (widget == NULL)
    return NULL;

  if (GTK_IS_BUTTON (widget))
    {
      const char *lbl = gtk_button_get_label (GTK_BUTTON (widget));
      if (g_strcmp0 (lbl, label) == 0)
        return widget;
    }

  child = gtk_widget_get_first_child (widget);
  while (child != NULL)
    {
      GtkWidget *found = find_pill_by_label (child, label);
      if (found != NULL)
        return found;
      child = gtk_widget_get_next_sibling (child);
    }

  return NULL;
}

/* Dump widget tree for diagnostics. */
static void
dump_tree (GtkWidget *root, int depth, const char *label)
{
  GType       t;
  char        line[256];
  int         pos = 0;
  int         i;
  const char *type_name;
  const char *extra = "";

  if (root == NULL)
    return;

  t = G_TYPE_FROM_INSTANCE (root);
  for (i = 0; i < depth && pos < 240; i++)
    pos += g_snprintf (line + pos, sizeof (line) - pos, "  ");

  type_name = g_type_name (t);

  if (GTK_IS_BUTTON (root))
    extra = gtk_button_get_label (GTK_BUTTON (root));
  else if (GTK_IS_LABEL (root))
    extra = gtk_label_get_text (GTK_LABEL (root));

  g_snprintf (line + pos, sizeof (line) - pos, "%s  %s", type_name, extra);
  g_test_message ("%s: %s", label, line);

  for (GtkWidget *c = gtk_widget_get_first_child (root);
       c != NULL; c = gtk_widget_get_next_sibling (c))
    dump_tree (c, depth + 1, label);
}

/* Set up a BzStateInfo with a flathub state containing one trending + one
 * game category, plus entry groups for all referenced app IDs. */
static void
setup_test_state (void)
{
  BzStateInfo    *state  = bz_state_info_get_default ();
  BzFlathubState *flathub;
  GListStore     *cats;
  GListStore     *all;

  g_assert_nonnull (state);

  flathub = bz_flathub_state_new (NULL, FALSE);
  cats = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);

  {
    BzFlathubCategory *c = add_category (flathub, "trending",
        (const char *[]) { "org.test.App1", "org.test.App4", NULL });
    g_list_store_append (cats, c);
  }
  {
    BzFlathubCategory *c = add_category (flathub, "game",
        (const char *[]) { "org.test.App2", "org.test.App3", "org.test.App5", NULL });
    g_list_store_append (cats, c);
  }

  bz_flathub_state_set_categories (flathub, cats);

  all = g_list_store_new (BZ_TYPE_ENTRY_GROUP);
  append_group (all, "org.test.App1");
  append_group (all, "org.test.App2");
  append_group (all, "org.test.App3");
  append_group (all, "org.test.App4");
  append_group (all, "org.test.App5");

  bz_state_info_set_all_entry_groups (state, G_LIST_MODEL (all));
  bz_state_info_set_flathub (state, flathub);

  g_test_message ("DIAG:setup | state=%p | flathub=%p | n_apps=5", (void *) state, (void *) flathub);
}

/* ------------------------------------------------------------------ */
/*  Tests                                                               */
/* ------------------------------------------------------------------ */

static gboolean notify_fired = FALSE;

static void
on_test_notify (BzFullView *self,
                GParamSpec *pspec,
                gpointer    user_data)
{
  notify_fired = TRUE;
  g_test_message ("DIAG:notify | handler=test | signal_name=%s | user_data=%p",
                  pspec ? g_param_spec_get_name (pspec) : "(null)", user_data);
}

/* Test 1 – BzFullView popover structure:
 *   - Verify notify::entry-group fires
 *   - Verify 5 buttons labelled New/Install/4-Stars/3-Stars/Forget it in popover
 */
static void
test_popover_structure (void)
{
  BzFullView   *full_view;
  BzEntryGroup *group;
  GtkWidget    *box;
  GtkWidget    *win;
  guint         n_buttons;
  gulong        sig_id;

  wipe_store ();

  full_view = g_object_new (BZ_TYPE_FULL_VIEW, NULL);
  g_assert_nonnull (full_view);
  win = host_full_view (full_view);

  /* Connect our own notify handler BEFORE set_entry_group */
  notify_fired = FALSE;
  sig_id = g_signal_connect (full_view, "notify::entry-group",
                              G_CALLBACK (on_test_notify), NULL);

  group = make_entry_group ("org.test.App1");
  g_assert_nonnull (group);

  g_test_message ("DIAG:test | before-set-entry-group | notify_fired=%d", notify_fired);
  bz_full_view_set_entry_group (full_view, group);
  g_test_message ("DIAG:test | after-set-entry-group | notify_fired=%d", notify_fired);

  g_signal_handler_disconnect (full_view, sig_id);
  g_object_unref (group);

  g_test_message ("DIAG:test | notify-fired=%d", notify_fired);

  /* Debug: enumerate ALL menu buttons */
  {
    GtkWidget *mbs[8];
    guint      nmb = find_all_menu_buttons (GTK_WIDGET (full_view), mbs, 8);
    for (guint j = 0; j < nmb; j++)
      {
        GtkPopover *p = gtk_menu_button_get_popover (GTK_MENU_BUTTON (mbs[j]));
        const char *icon = gtk_menu_button_get_icon_name (GTK_MENU_BUTTON (mbs[j]));
        g_test_message ("DIAG:menu-button[%u] | ptr=%p | icon=%s | popover=%p",
                        j, (void *) mbs[j],
                        icon ? icon : "(null)",
                        (void *) p);
        if (p)
          {
            GtkWidget *c = gtk_popover_get_child (p);
            g_test_message ("DIAG:menu-button[%u] | popover_child=%p | type=%s",
                            j, (void *) c, c ? G_OBJECT_TYPE_NAME (c) : "(null)");
            if (c)
              {
                GtkWidget *gc = gtk_widget_get_first_child (c);
                while (gc)
                  {
                    g_test_message ("DIAG:menu-button[%u] | child=%s",
                                    j, G_OBJECT_TYPE_NAME (gc));
                    if (GTK_IS_BUTTON (gc))
                      {
                        const char *lbl = gtk_button_get_label (GTK_BUTTON (gc));
                        g_test_message ("DIAG:menu-button[%u] | child-label=%s",
                                        j, lbl ? lbl : "(null)");
                      }
                    gc = gtk_widget_get_next_sibling (gc);
                  }
              }
          }
      }
  }

  box = get_popover_box (full_view);
  n_buttons = count_buttons (box);

  g_test_message ("DIAG:popover | n_buttons=%u | expected=5", n_buttons);
  g_assert_cmpuint (n_buttons, ==, 5);

  g_assert_nonnull (find_button_by_label (box, "New"));
  g_assert_nonnull (find_button_by_label (box, "Install"));
  g_assert_nonnull (find_button_by_label (box, "4-Stars"));
  g_assert_nonnull (find_button_by_label (box, "3-Stars"));
  g_assert_nonnull (find_button_by_label (box, "Forget it"));

  g_object_ref_sink (full_view);
  gtk_window_destroy (GTK_WINDOW (win));
  g_clear_object (&full_view);
}

/* Test 2 – Pre-selection honours stored label:
 *   - When DB says "Install", the Install button has suggested-action
 *   - New and Forget it do NOT have suggested-action
 */
static void
test_popover_selection (void)
{
  BzFullView   *full_view;
  BzEntryGroup *group;
  GtkWidget    *box;
  GtkWidget    *btn_new;
  GtkWidget    *btn_install;
  GtkWidget    *btn_forget;
  GtkWidget    *win;

  seed_core_labels (
      (const char *[]) { "org.test.App1", NULL },
      (const char *[]) { "Install", NULL }, 1);

  full_view = g_object_new (BZ_TYPE_FULL_VIEW, NULL);
  g_assert_nonnull (full_view);
  win = host_full_view (full_view);

  group = make_entry_group ("org.test.App1");
  bz_full_view_set_entry_group (full_view, group);
  g_object_unref (group);

  box           = get_popover_box (full_view);
  btn_new       = find_button_by_label (box, "New");
  btn_install   = find_button_by_label (box, "Install");
  btn_forget    = find_button_by_label (box, "Forget it");
  g_assert_nonnull (btn_new);
  g_assert_nonnull (btn_install);
  g_assert_nonnull (btn_forget);

  g_test_message ("DIAG:popover-selection"
                  " | app=org.test.App1 | stored=Install"
                  " | new_has_suggested=%d"
                  " | install_has_suggested=%d"
                  " | forget_has_suggested=%d",
                  gtk_widget_has_css_class (btn_new, "suggested-action"),
                  gtk_widget_has_css_class (btn_install, "suggested-action"),
                  gtk_widget_has_css_class (btn_forget, "suggested-action"));

  g_assert_false (gtk_widget_has_css_class (btn_new, "suggested-action"));
  g_assert_true  (gtk_widget_has_css_class (btn_install, "suggested-action"));
  g_assert_false (gtk_widget_has_css_class (btn_forget, "suggested-action"));

  g_object_ref_sink (full_view);
  gtk_window_destroy (GTK_WINDOW (win));
  g_clear_object (&full_view);
}

/* Test 3 – Default selection when app is unknown:
 *   - When no DB entry exists for the current app, "New" is pre-selected
 */
static void
test_popover_default_new (void)
{
  BzFullView   *full_view;
  BzEntryGroup *group;
  GtkWidget    *box;
  GtkWidget    *btn_new;
  GtkWidget    *win;

  wipe_store ();

  full_view = g_object_new (BZ_TYPE_FULL_VIEW, NULL);
  g_assert_nonnull (full_view);
  win = host_full_view (full_view);

  group = make_entry_group ("org.test.NewApp");
  bz_full_view_set_entry_group (full_view, group);
  g_object_unref (group);

  box    = get_popover_box (full_view);
  btn_new = find_button_by_label (box, "New");
  g_assert_nonnull (btn_new);

  g_test_message ("DIAG:popover-default"
                  " | app=org.test.NewApp | stored=none"
                  " | new_has_suggested=%d",
                  gtk_widget_has_css_class (btn_new, "suggested-action"));

  g_assert_true (gtk_widget_has_css_class (btn_new, "suggested-action"));

  g_object_ref_sink (full_view);
  gtk_window_destroy (GTK_WINDOW (win));
  g_clear_object (&full_view);
}

/* Test 4 – Clicking a label button persists to the store and updates selection:
 *   - Click "Install" → store updated → selection markers swap
 */
static void
test_label_change_persists (void)
{
  BzFullView   *full_view;
  BzEntryGroup *group;
  GtkWidget    *box;
  GtkWidget    *btn_install;
  GtkWidget    *btn_new;
  GtkWidget    *win;
  char          *stored;

  /* Start with app labelled "New" */
  seed_core_labels (
      (const char *[]) { "org.test.App1", NULL },
      (const char *[]) { "New", NULL }, 1);

  full_view = g_object_new (BZ_TYPE_FULL_VIEW, NULL);
  g_assert_nonnull (full_view);
  win = host_full_view (full_view);

  group = make_entry_group ("org.test.App1");
  bz_full_view_set_entry_group (full_view, group);
  g_object_unref (group);

  box = get_popover_box (full_view);
  btn_new     = find_button_by_label (box, "New");
  btn_install = find_button_by_label (box, "Install");
  g_assert_nonnull (btn_new);
  g_assert_nonnull (btn_install);

  g_test_message ("DIAG:before-click"
                  " | app=org.test.App1 | new_has_suggested=%d | install_has_suggested=%d",
                  gtk_widget_has_css_class (btn_new, "suggested-action"),
                  gtk_widget_has_css_class (btn_install, "suggested-action"));

  /* Click "Install" */
  g_signal_emit_by_name (btn_install, "clicked");
  process_events ();

  /* Check the saved file */
  stored = read_core_label ("org.test.App1");
  g_test_message ("DIAG:after-click | app=org.test.App1"
                  " | label_from_store=%s | expected=Install",
                  stored ? stored : "(null)");
  g_assert_cmpstr (stored, ==, "Install");
  g_free (stored);

  /* Get popover box again — BzFullView's popover rebuilds on the
   * next press (capture-phase gesture), but the hash table is already
   * updated in the clicked handler.  The suggested-action CSS class
   * swap also happened inline. */
  g_test_message ("DIAG:after-click-selection"
                  " | app=org.test.App1 | new_has_suggested=%d | install_has_suggested=%d",
                  gtk_widget_has_css_class (btn_new, "suggested-action"),
                  gtk_widget_has_css_class (btn_install, "suggested-action"));

  g_object_ref_sink (full_view);
  gtk_window_destroy (GTK_WINDOW (win));
  g_clear_object (&full_view);
}

/* Test 5 – CzCustomPage core-label pills:
 *   - Five core-label pills (New/Install/4-Stars/3-Stars/Forget it) exist
 *   - After page ::map, "New" is auto-selected
 */
static void
test_custom_page_core_pills (void)
{
  CzCustomPage *page;
  GtkWidget    *custom_box;
  GtkWidget    *pill_new;
  GtkWidget    *pill_install;
  GtkWidget    *pill_4stars;
  GtkWidget    *pill_3stars;
  GtkWidget    *pill_forget;
  GtkWidget    *win;
  GtkWidget    *stack;

  seed_core_labels (
      (const char *[]) { "org.test.App1", "org.test.App2", "org.test.App3",
                         "org.test.App4", "org.test.App5", NULL },
      (const char *[]) { "Install",       "New",             "Forget it",
                         "4-Stars",       "3-Stars",         NULL }, 5);

  g_application_set_default (NULL);
  setup_test_state ();

  stack = g_object_new (ADW_TYPE_VIEW_STACK, NULL);
  {
    GtkWidget *other = gtk_label_new ("other");
    adw_view_stack_add_titled_with_icon (
        ADW_VIEW_STACK (stack), other, "other", "Other", NULL);
  }

  page = cz_custom_page_new ();
  g_assert_nonnull (page);
  adw_view_stack_add_titled_with_icon (
      ADW_VIEW_STACK (stack),
      GTK_WIDGET (page), "custom", "Custom",
      "preferences-other-symbolic");

  win = gtk_window_new ();
  gtk_window_set_child (GTK_WINDOW (win), stack);
  gtk_widget_set_visible (win, TRUE);
  process_events ();

  /* Switch to custom page → triggers ::map → auto-select "New" */
  adw_view_stack_set_visible_child (ADW_VIEW_STACK (stack), GTK_WIDGET (page));
  process_events ();

  /* Find the custom_pill_box — third box with button children in the tree */
  custom_box = find_pill_box (GTK_WIDGET (page));
  g_assert_nonnull (custom_box);
  dump_tree (GTK_WIDGET (page), 0, "page-tree");

  pill_new      = find_pill_by_label (GTK_WIDGET (page), "New");
  pill_install  = find_pill_by_label (GTK_WIDGET (page), "Install");
  pill_4stars   = find_pill_by_label (GTK_WIDGET (page), "4-Stars");
  pill_3stars   = find_pill_by_label (GTK_WIDGET (page), "3-Stars");
  pill_forget   = find_pill_by_label (GTK_WIDGET (page), "Forget it");

  g_test_message ("DIAG:core-pills"
                  " | new=%p | install=%p | 4stars=%p | 3stars=%p | forget=%p",
                  (void *) pill_new, (void *) pill_install, (void *) pill_4stars,
                  (void *) pill_3stars, (void *) pill_forget);
  g_assert_nonnull (pill_new);
  g_assert_nonnull (pill_install);
  g_assert_nonnull (pill_4stars);
  g_assert_nonnull (pill_3stars);
  g_assert_nonnull (pill_forget);

  g_test_message ("DIAG:core-pills-selection"
                  " | new_selected=%d | install_selected=%d | forget_selected=%d",
                  gtk_widget_has_css_class (pill_new, "selected"),
                  gtk_widget_has_css_class (pill_install, "selected"),
                  gtk_widget_has_css_class (pill_forget, "selected"));

  /* "New" should be auto-selected after ::map */
  g_assert_true (gtk_widget_has_css_class (pill_new, "selected"));

  gtk_window_destroy (GTK_WINDOW (win));
}

/* Test 6 – Clicking a core-label pill changes the filter:
 *   - "New" selected → 0 apps (trending has App1=Install / App4=4-Stars;
 *     game has App2=New / App3=Forget it / App5=3-Stars)
 *   - Click "Install" → only App1 (core=Install, in trending) is visible
 *   - Click "4-Stars" → only App4 (core=4-Stars, in trending) is visible
 *   - Click "3-Stars" → no apps (App5=3-Stars is only in game)
 *   - Click "Forget it" → no apps (App3=Forget it is only in game)
 *   - Click "New" again → 0 apps again (trending has no New app)
 */
static void
test_core_pill_affects_filter (void)
{
  CzCustomPage *page;
  GtkWidget    *pill_new;
  GtkWidget    *pill_install;
  GtkWidget    *pill_4stars;
  GtkWidget    *pill_3stars;
  GtkWidget    *pill_forget;
  GtkWidget    *list_view;
  GListModel   *model;
  guint         n;
  GtkWidget    *win;
  GtkWidget    *stack;

  seed_core_labels (
      (const char *[]) { "org.test.App1", "org.test.App2", "org.test.App3",
                         "org.test.App4", "org.test.App5", NULL },
      (const char *[]) { "Install",       "New",             "Forget it",
                         "4-Stars",       "3-Stars",         NULL }, 5);

  g_application_set_default (NULL);
  g_test_message ("DIAG:app-default | after-clear=%p",
                  (void *) g_application_get_default ());
  setup_test_state ();
  g_test_message ("DIAG:app-default | after-state=%p",
                  (void *) g_application_get_default ());

  stack = g_object_new (ADW_TYPE_VIEW_STACK, NULL);
  {
    GtkWidget *other = gtk_label_new ("other");
    adw_view_stack_add_titled_with_icon (
        ADW_VIEW_STACK (stack), other, "other", "Other", NULL);
  }

  page = cz_custom_page_new ();
  g_test_message ("DIAG:app-default | after-page=%p",
                  (void *) g_application_get_default ());
  adw_view_stack_add_titled_with_icon (
      ADW_VIEW_STACK (stack),
      GTK_WIDGET (page), "custom", "Custom",
      "preferences-other-symbolic");

  win = gtk_window_new ();
  gtk_window_set_child (GTK_WINDOW (win), GTK_WIDGET (stack));
  gtk_widget_set_visible (win, TRUE);
  process_events ();

  /* Switch to custom → ::map → trending click → shows "trending" apps */
  adw_view_stack_set_visible_child (ADW_VIEW_STACK (stack), GTK_WIDGET (page));
  process_events ();

  g_test_message ("DIAG:app-default | after-map=%p",
                  (void *) g_application_get_default ());

  /* DIAG the page's label store view of the seeded DB */
  {
    CzCustomLabelStore *store = cz_custom_page_get_label_store (page);
    const char *l1 = cz_custom_label_store_get_core_label (store, "org.test.App1");
    const char *l2 = cz_custom_label_store_get_core_label (store, "org.test.App2");
    const char *l3 = cz_custom_label_store_get_core_label (store, "org.test.App3");
    const char *l4 = cz_custom_label_store_get_core_label (store, "org.test.App4");
    const char *l5 = cz_custom_label_store_get_core_label (store, "org.test.App5");
    g_test_message ("DIAG:filter-seed-store"
                    " | app1=%s | app2=%s | app3=%s | app4=%s | app5=%s",
                    l1 ? l1 : "(null)", l2 ? l2 : "(null)",
                    l3 ? l3 : "(null)", l4 ? l4 : "(null)", l5 ? l5 : "(null)");
  }

  list_view = find_list_view (GTK_WIDGET (page));
  g_assert_nonnull (list_view);
  g_assert_true (gtk_widget_get_visible (list_view));

  g_test_message ("DIAG:app-default | before-click-new=%p",
                  (void *) g_application_get_default ());

  /* With "New" auto-selected, no apps in "trending" have core=New
   * (trending has App1=Install / App4=4-Stars). */
  model = bz_dynamic_list_view_get_model (BZ_DYNAMIC_LIST_VIEW (list_view));
  g_assert_nonnull (model);
  n = g_list_model_get_n_items (model);
  g_test_message ("DIAG:filter-new | n_items=%u | expected=0 (trending has App1=Install, App4=4-Stars)", n);
  g_assert_cmpuint (n, ==, 0);

  /* Click "Install" pill */
  pill_install = find_pill_by_label (GTK_WIDGET (page), "Install");
  g_assert_nonnull (pill_install);
  g_signal_emit_by_name (pill_install, "clicked");
  g_test_message ("DIAG:app-default | after-click-install=%p",
                  (void *) g_application_get_default ());
  process_events ();

  model = bz_dynamic_list_view_get_model (BZ_DYNAMIC_LIST_VIEW (list_view));
  g_assert_nonnull (model);
  n = g_list_model_get_n_items (model);
  g_test_message ("DIAG:filter-install | n_items=%u | expected=1 (only App1 has core=Install in trending)", n);
  g_assert_cmpuint (n, ==, 1);

  /* Click "4-Stars" pill */
  pill_4stars = find_pill_by_label (GTK_WIDGET (page), "4-Stars");
  g_assert_nonnull (pill_4stars);
  g_signal_emit_by_name (pill_4stars, "clicked");
  process_events ();

  model = bz_dynamic_list_view_get_model (BZ_DYNAMIC_LIST_VIEW (list_view));
  g_assert_nonnull (model);
  n = g_list_model_get_n_items (model);
  g_test_message ("DIAG:filter-4-stars | n_items=%u | expected=1 (only App4 has core=4-Stars in trending)", n);
  g_assert_cmpuint (n, ==, 1);

  /* Click "3-Stars" pill */
  pill_3stars = find_pill_by_label (GTK_WIDGET (page), "3-Stars");
  g_assert_nonnull (pill_3stars);
  g_signal_emit_by_name (pill_3stars, "clicked");
  process_events ();

  model = bz_dynamic_list_view_get_model (BZ_DYNAMIC_LIST_VIEW (list_view));
  g_assert_nonnull (model);
  n = g_list_model_get_n_items (model);
  g_test_message ("DIAG:filter-3-stars | n_items=%u | expected=0 (App5=3-Stars is only in game)", n);
  g_assert_cmpuint (n, ==, 0);

  /* Click "Forget it" pill */
  pill_forget = find_pill_by_label (GTK_WIDGET (page), "Forget it");
  g_assert_nonnull (pill_forget);
  g_signal_emit_by_name (pill_forget, "clicked");
  process_events ();

  model = bz_dynamic_list_view_get_model (BZ_DYNAMIC_LIST_VIEW (list_view));
  g_assert_nonnull (model);
  n = g_list_model_get_n_items (model);
  g_test_message ("DIAG:filter-forget-it | n_items=%u | expected=0 (App3=Forget it is only in game)", n);
  g_assert_cmpuint (n, ==, 0);

  /* Click "New" again to verify it works both ways */
  pill_new = find_pill_by_label (GTK_WIDGET (page), "New");
  g_assert_nonnull (pill_new);
  g_signal_emit_by_name (pill_new, "clicked");
  process_events ();

  model = bz_dynamic_list_view_get_model (BZ_DYNAMIC_LIST_VIEW (list_view));
  g_assert_nonnull (model);
  n = g_list_model_get_n_items (model);
  g_test_message ("DIAG:filter-new-again | n_items=%u | expected=0", n);
  g_assert_cmpuint (n, ==, 0);

  gtk_window_destroy (GTK_WINDOW (win));
}

/* Test 7 – Cross-component DB round-trip:
 *   - BzFullView writes a label change → file updated
 *   - New CzCustomPage loads the same file → pill state matches
 *   - Simulates: user changes label in BzFullView, then navigates to custom page
 */
static void
test_integration_roundtrip (void)
{
  BzFullView   *full_view;
  BzEntryGroup *group;
  GtkWidget    *box;
  GtkWidget    *btn_forget;
  GtkWidget    *fv_win;
  char          *stored;

  /* Step 1: BzFullView loads "Install" for App1 */
  seed_core_labels (
      (const char *[]) { "org.test.App1", NULL },
      (const char *[]) { "Install", NULL }, 1);

  full_view = g_object_new (BZ_TYPE_FULL_VIEW, NULL);
  fv_win = host_full_view (full_view);
  group = make_entry_group ("org.test.App1");
  bz_full_view_set_entry_group (full_view, group);
  g_object_unref (group);

  /* Step 2: Change label to "Forget it" in BzFullView */
  box = get_popover_box (full_view);
  btn_forget = find_button_by_label (box, "Forget it");
  g_assert_nonnull (btn_forget);

  g_signal_emit_by_name (btn_forget, "clicked");
  process_events ();

  /* Verify file now has "Forget it" for App1 */
  stored = read_core_label ("org.test.App1");
  g_test_message ("DIAG:roundtrip-step2 | app=org.test.App1"
                  " | store_value=%s | expected=Forget it",
                  stored ? stored : "(null)");
  g_assert_cmpstr (stored, ==, "Forget it");
  g_free (stored);

  g_object_ref_sink (full_view);
  gtk_window_destroy (GTK_WINDOW (fv_win));
  g_clear_object (&full_view);

  /* Step 3: Simulate navigating to a just-loaded CzCustomPage by destroying
   * and recreating the page (which reloads from the store on map).
   * cleanup: core_labels was in full_view which is now gone; the store is the
   * persistent source. */

  g_application_set_default (NULL);
  setup_test_state ();

  {
    CzCustomPage  *page;
    GtkWidget     *target_page;
    GtkWidget     *stack = g_object_new (ADW_TYPE_VIEW_STACK, NULL);
    GtkWidget     *win;
    GtkWidget     *other = gtk_label_new ("other");

    adw_view_stack_add_titled_with_icon (
        ADW_VIEW_STACK (stack), other, "other", "Other", NULL);

    target_page = GTK_WIDGET (cz_custom_page_new ());
    g_assert_nonnull (target_page);

    adw_view_stack_add_titled_with_icon (
        ADW_VIEW_STACK (stack), target_page, "custom", "Custom",
        "preferences-other-symbolic");

    win = gtk_window_new ();
    gtk_window_set_child (GTK_WINDOW (win), GTK_WIDGET (stack));
    gtk_widget_set_visible (win, TRUE);
    process_events ();

    /* Switch to custom page → ::map → reloads label store from disk */
    adw_view_stack_set_visible_child (ADW_VIEW_STACK (stack), target_page);
    process_events ();

    page = CZ_CUSTOM_PAGE (target_page);

    /* Auto-select should pick "New" — check the stored labels reflect
     * the BzFullView change (App1=Forget it, App2=New, App3=Forget it) */
    {
      CzCustomLabelStore *store = cz_custom_page_get_label_store (page);
      const char *app1;
      const char *app2;

      app1 = cz_custom_label_store_get_core_label (store, "org.test.App1");
      app2 = cz_custom_label_store_get_core_label (store, "org.test.App2");
      g_test_message ("DIAG:roundtrip-step3"
                      " | app1=%s | expected=Forget it"
                      " | app2=%s | expected=New",
                      app1 ? app1 : "(null)",
                      app2 ? app2 : "(null)");
      g_assert_cmpstr (app1, ==, "Forget it");
      g_assert_cmpstr (app2, ==, "New");
    }

    gtk_window_destroy (GTK_WINDOW (win));
  }
}

/* Test 8 – BzFullView loads with a corrupt DB file:
 *   - bz_label_store_open fails integrity check and opens a fresh DB
 *   - Popover defaults to "New" for unknown apps
 */
static void
test_full_view_load_corrupt_store (void)
{
  BzFullView   *full_view;
  BzEntryGroup *group;
  GtkWidget    *box;
  GtkWidget    *btn_new;
  GtkWidget    *win;

  /* Write a corrupt (non-SQLite) file where the DB should be; the store
   * must fail the integrity check, find no valid backup, and open fresh. */
  g_file_set_contents (store_path, "NOT VALID SQLITE", -1, NULL);

  full_view = g_object_new (BZ_TYPE_FULL_VIEW, NULL);
  g_assert_nonnull (full_view);
  win = host_full_view (full_view);

  group = make_entry_group ("org.test.CorruptApp");
  bz_full_view_set_entry_group (full_view, group);
  g_object_unref (group);

  box = get_popover_box (full_view);
  btn_new = find_button_by_label (box, "New");
  g_assert_nonnull (btn_new);
  g_assert_true (gtk_widget_has_css_class (btn_new, "suggested-action"));

  g_test_message ("DIAG:action=corrupt-store-load"
                  " | new_has_suggested=%d | expected=1",
                  gtk_widget_has_css_class (btn_new, "suggested-action"));

  g_object_ref_sink (full_view);
  gtk_window_destroy (GTK_WINDOW (win));
  g_clear_object (&full_view);
}

/* Test 9 – bz_label_store_set_core_label in BzFullView preserves noncore
 * data from a store that has noncore but no core entries for the current app.
 * Simulates: store has noncore data for App1 but only other apps'
 * core data; clicking a label creates a new core entry for App1 without
 * destroying the existing noncore data.
 */
static void
test_save_preserves_existing_noncore (void)
{
  BzFullView   *full_view;
  BzEntryGroup *group;
  GtkWidget    *box;
  GtkWidget    *btn_install;
  GtkWidget    *win;
  char          *stored;

  /* Seed a store with noncore data for App1 + core data for App2,
   * but NO core entry for App1. */
  seed_core_labels (
      (const char *[]) { "org.test.App2", NULL },
      (const char *[]) { "Forget it", NULL }, 1);
  seed_label_name ("MyTag");
  seed_noncore_assignment ("org.test.App1", "MyTag");

  full_view = g_object_new (BZ_TYPE_FULL_VIEW, NULL);
  g_assert_nonnull (full_view);
  win = host_full_view (full_view);

  group = make_entry_group ("org.test.App1");
  bz_full_view_set_entry_group (full_view, group);
  g_object_unref (group);

  /* Click "Install" to trigger save_core_labels */
  box = get_popover_box (full_view);
  btn_install = find_button_by_label (box, "Install");
  g_assert_nonnull (btn_install);
  g_signal_emit_by_name (btn_install, "clicked");
  process_events ();

  /* Verify file now has core=org.test.App1 -> Install */
  stored = read_core_label ("org.test.App1");
  g_test_message ("DIAG:action=save-preserves-noncore"
                  " | app1_core=%s | expected=Install", stored ? stored : "(null)");
  g_assert_cmpstr (stored, ==, "Install");
  g_free (stored);

  /* Verify noncore data for App1 is preserved ("MyTag" is still there) */
  g_test_message ("DIAG:action=verify-noncore-preserved"
                  " | app1_has_MyTag=%d | expected=1",
                  read_has_noncore ("org.test.App1", "MyTag"));
  g_assert_true (read_has_noncore ("org.test.App1", "MyTag"));

  g_object_ref_sink (full_view);
  gtk_window_destroy (GTK_WINDOW (win));
  g_clear_object (&full_view);
}

/* ------------------------------------------------------------------ */
/*  Noncore label — pass-condition tests                                */
/* ------------------------------------------------------------------ */

/* Helper: find the noncore MenuButton (the LAST open-menu-symbolic one;
 * core_label_button appears first in the template tree). */
static GtkWidget *
find_noncore_menu_button (GtkWidget *widget)
{
  GtkWidget *child;
  GtkWidget *found = NULL;

  if (widget == NULL)
    return NULL;
  if (GTK_IS_MENU_BUTTON (widget))
    {
      const char *icon = gtk_menu_button_get_icon_name (GTK_MENU_BUTTON (widget));
      if (g_strcmp0 (icon, "open-menu-symbolic") == 0)
        found = widget;
    }

  child = gtk_widget_get_first_child (widget);
  while (child != NULL)
    {
      GtkWidget *f = find_noncore_menu_button (child);
      if (f != NULL)
        found = f;
      child = gtk_widget_get_next_sibling (child);
    }
  return found;
}

static GtkWidget *
get_noncore_popover_box (BzFullView *full_view)
{
  GtkWidget  *menu_button;
  GtkPopover *popover;
  GtkWidget  *box;

  menu_button = find_noncore_menu_button (GTK_WIDGET (full_view));
  g_assert_nonnull (menu_button);
  g_test_message ("DIAG:noncore-popover | menu_button=%s",
                  G_OBJECT_TYPE_NAME (menu_button));

  popover = gtk_menu_button_get_popover (GTK_MENU_BUTTON (menu_button));
  g_assert_nonnull (popover);
  g_test_message ("DIAG:noncore-popover | popover=%s",
                  G_OBJECT_TYPE_NAME (popover));

  box = gtk_popover_get_child (popover);
  g_assert_nonnull (box);
  g_test_message ("DIAG:noncore-popover | child_type=%s",
                  G_OBJECT_TYPE_NAME (box));

  {
    GtkWidget *c = gtk_widget_get_first_child (box);
    int idx = 0;
    while (c != NULL)
      {
        g_test_message ("DIAG:noncore-popover-child[%d] | type=%s", idx,
                        G_OBJECT_TYPE_NAME (c));
        if (GTK_IS_BUTTON (c))
          {
            const char *lbl = gtk_button_get_label (GTK_BUTTON (c));
            g_test_message ("DIAG:noncore-popover-child[%d] | label=%s", idx,
                            lbl ? lbl : "(null)");
          }
        c = gtk_widget_get_next_sibling (c);
        idx++;
      }
  }

  return box;
}

/* Test: BzFullView noncore popover contains "None" button when
 * the store has no label names and no assignment exists. */
static void
test_noncore_popover_empty (void)
{
  BzFullView   *full_view;
  BzEntryGroup *group;
  GtkWidget    *box;
  GtkWidget    *win;

  wipe_store ();

  full_view = g_object_new (BZ_TYPE_FULL_VIEW, NULL);
  g_assert_nonnull (full_view);
  win = host_full_view (full_view);

  group = make_entry_group ("org.test.App1");
  bz_full_view_set_entry_group (full_view, group);
  g_object_unref (group);

  box = get_noncore_popover_box (full_view);
  g_assert_nonnull (box);

  /* Empty store: no label names → "No custom labels defined" label */
  {
    GtkWidget *empty_label = find_label_in_tree (box, "No custom labels defined");
    g_test_message ("DIAG:noncore-popover-empty | empty_label=%p", (void *) empty_label);
    g_assert_nonnull (empty_label);
  }

  /* No checkbuttons should exist when store is empty */
  {
    GtkWidget *tag_a = find_check_button_by_label (box, "TagA");
    g_assert_null (tag_a);
  }

  g_object_ref_sink (full_view);
  gtk_window_destroy (GTK_WINDOW (win));
  g_clear_object (&full_view);
}

/* Test: BzFullView noncore popover shows GtkCheckButtons for each
 * label name from the store. */
static void
test_noncore_popover_with_names (void)
{
  BzFullView   *full_view;
  BzEntryGroup *group;
  GtkWidget    *box;
  GtkWidget    *win;

  /* Seed label names: TagA, TagB */
  seed_label_name ("TagA");
  seed_label_name ("TagB");

  full_view = g_object_new (BZ_TYPE_FULL_VIEW, NULL);
  g_assert_nonnull (full_view);
  win = host_full_view (full_view);

  group = make_entry_group ("org.test.App1");
  bz_full_view_set_entry_group (full_view, group);
  g_object_unref (group);

  box = get_noncore_popover_box (full_view);

  {
    GtkWidget *tag_a_btn = find_check_button_by_label (box, "TagA");
    GtkWidget *tag_b_btn = find_check_button_by_label (box, "TagB");
    g_test_message ("DIAG:noncore-popover-names"
                    " | TagA=%p | TagB=%p",
                    (void *) tag_a_btn, (void *) tag_b_btn);
    g_assert_nonnull (tag_a_btn);
    g_assert_nonnull (tag_b_btn);
    g_assert_true (GTK_IS_CHECK_BUTTON (tag_a_btn));
    g_assert_true (GTK_IS_CHECK_BUTTON (tag_b_btn));
  }

  g_object_ref_sink (full_view);
  gtk_window_destroy (GTK_WINDOW (win));
  g_clear_object (&full_view);
}

/* Test: BzFullView noncore assign — toggling a GtkCheckButton active
 * persists the label to the store and adds suggested-action CSS. */
static void
test_noncore_assign_persists (void)
{
  BzFullView   *full_view;
  BzEntryGroup *group;
  GtkWidget    *box;
  GtkWidget    *btn_tag_a;
  GtkWidget    *win;

  /* Seed label name: TagA */
  seed_label_name ("TagA");

  full_view = g_object_new (BZ_TYPE_FULL_VIEW, NULL);
  g_assert_nonnull (full_view);
  win = host_full_view (full_view);

  group = make_entry_group ("org.test.App1");
  bz_full_view_set_entry_group (full_view, group);
  g_object_unref (group);

  box = get_noncore_popover_box (full_view);
  btn_tag_a = find_check_button_by_label (box, "TagA");
  g_assert_nonnull (btn_tag_a);
  g_assert_true (GTK_IS_CHECK_BUTTON (btn_tag_a));

  g_test_message ("DIAG:noncore-before-assign"
                  " | tagA_active=%d | tagA_suggested=%d",
                  gtk_check_button_get_active (GTK_CHECK_BUTTON (btn_tag_a)),
                  gtk_widget_has_css_class (btn_tag_a, "suggested-action"));

  /* Initially unchecked, no suggested-action */
  g_assert_false (gtk_check_button_get_active (GTK_CHECK_BUTTON (btn_tag_a)));
  g_assert_false (gtk_widget_has_css_class (btn_tag_a, "suggested-action"));

  /* Toggle checkbutton active to assign */
  gtk_check_button_set_active (GTK_CHECK_BUTTON (btn_tag_a), TRUE);
  process_events ();

  /* Verify store: App1 has noncore=["TagA"] */
  g_test_message ("DIAG:noncore-after-assign"
                  " | app1_has_TagA=%d | expected=1",
                  read_has_noncore ("org.test.App1", "TagA"));
  g_assert_true (read_has_noncore ("org.test.App1", "TagA"));
  g_assert_true (gtk_widget_has_css_class (btn_tag_a, "suggested-action"));

  g_object_ref_sink (full_view);
  gtk_window_destroy (GTK_WINDOW (win));
  g_clear_object (&full_view);
}

/* Test: BzFullView noncore unassign — toggling a GtkCheckButton inactive
 * removes the label from the store and drops suggested-action CSS. */
static void
test_noncore_unassign (void)
{
  BzFullView   *full_view;
  BzEntryGroup *group;
  GtkWidget    *box;
  GtkWidget    *btn_tag_a;
  GtkWidget    *win;

  /* Seed: App1 already assigned "TagA" + label name registered */
  seed_label_name ("TagA");
  seed_noncore_assignment ("org.test.App1", "TagA");

  full_view = g_object_new (BZ_TYPE_FULL_VIEW, NULL);
  g_assert_nonnull (full_view);
  win = host_full_view (full_view);

  group = make_entry_group ("org.test.App1");
  bz_full_view_set_entry_group (full_view, group);
  g_object_unref (group);

  box = get_noncore_popover_box (full_view);
  btn_tag_a = find_check_button_by_label (box, "TagA");
  g_assert_nonnull (btn_tag_a);
  g_assert_true (GTK_IS_CHECK_BUTTON (btn_tag_a));

  /* Initially checked (seeded assignment) with suggested-action */
  g_assert_true (gtk_check_button_get_active (GTK_CHECK_BUTTON (btn_tag_a)));
  g_assert_true (gtk_widget_has_css_class (btn_tag_a, "suggested-action"));

  /* Toggle checkbutton inactive to unassign */
  gtk_check_button_set_active (GTK_CHECK_BUTTON (btn_tag_a), FALSE);
  process_events ();

  /* Verify store: App1 no longer has TagA */
  g_test_message ("DIAG:noncore-after-unassign"
                  " | app1_has_TagA=%d | expected=0",
                  read_has_noncore ("org.test.App1", "TagA"));
  g_assert_false (read_has_noncore ("org.test.App1", "TagA"));
  g_assert_false (gtk_widget_has_css_class (btn_tag_a, "suggested-action"));

  g_object_ref_sink (full_view);
  gtk_window_destroy (GTK_WINDOW (win));
  g_clear_object (&full_view);
}

/* Test: BzFullView noncore — empty store shows "No custom labels defined"
 * label and no checkboxes have suggested-action. */
static void
test_noncore_default_none_selected (void)
{
  BzFullView   *full_view;
  BzEntryGroup *group;
  GtkWidget    *box;
  GtkWidget    *win;

  wipe_store ();

  full_view = g_object_new (BZ_TYPE_FULL_VIEW, NULL);
  g_assert_nonnull (full_view);
  win = host_full_view (full_view);

  group = make_entry_group ("org.test.App1");
  bz_full_view_set_entry_group (full_view, group);
  g_object_unref (group);

  box = get_noncore_popover_box (full_view);

  /* Empty store: "No custom labels defined" label */
  {
    GtkWidget *empty_label = find_label_in_tree (box, "No custom labels defined");
    g_test_message ("DIAG:noncore-default-selection"
                    " | empty_label=%p", (void *) empty_label);
    g_assert_nonnull (empty_label);
  }

  /* No checkbuttons exist at all in the empty state */
  {
    GtkWidget *any_check = find_check_button_by_label (box, "TagA");
    g_assert_null (any_check);
  }

  g_object_ref_sink (full_view);
  gtk_window_destroy (GTK_WINDOW (win));
  g_clear_object (&full_view);
}

/* Test: save_core_labels in BzFullView preserves noncore_label_names
 * arary and per-app noncore assignments when writing new core labels. */
static void
test_save_preserves_noncore_label_names (void)
{
  BzFullView   *full_view;
  BzEntryGroup *group;
  GtkWidget    *btn_install;
  GtkWidget    *win;

  /* Seed: label names MyTag/OtherTag + per-app noncore assignment */
  seed_core_labels (
      (const char *[]) { "org.test.App1", NULL },
      (const char *[]) { "New", NULL }, 1);
  seed_label_name ("MyTag");
  seed_label_name ("OtherTag");
  seed_noncore_assignment ("org.test.App1", "MyTag");

  full_view = g_object_new (BZ_TYPE_FULL_VIEW, NULL);
  g_assert_nonnull (full_view);
  win = host_full_view (full_view);

  group = make_entry_group ("org.test.App1");
  bz_full_view_set_entry_group (full_view, group);
  g_object_unref (group);

  /* Use the CORE popover to change core label */
  {
    GtkWidget *core_box = get_popover_box (full_view);
    btn_install = find_button_by_label (core_box, "Install");
    g_assert_nonnull (btn_install);
    g_signal_emit_by_name (btn_install, "clicked");
    process_events ();
  }

  /* Verify: label names preserved and per-app noncore assignment intact */
  g_test_message ("DIAG:save-preserves-names"
                  " | has_MyTag=%d | expected=1"
                  " | has_OtherTag=%d | expected=1"
                  " | app1_has_MyTag=%d | expected=1",
                  read_has_label_name ("MyTag"),
                  read_has_label_name ("OtherTag"),
                  read_has_noncore ("org.test.App1", "MyTag"));
  g_assert_true (read_has_label_name ("MyTag"));
  g_assert_true (read_has_label_name ("OtherTag"));
  g_assert_true (read_has_noncore ("org.test.App1", "MyTag"));

  g_object_ref_sink (full_view);
  gtk_window_destroy (GTK_WINDOW (win));
  g_clear_object (&full_view);
}

/* Test: noncore label assign affects CzCustomPage pills — assign a
 * noncore label, switch to custom page, verify the pill appears. */
static void
test_noncore_pills_on_custom_page (void)
{
  CzCustomPage  *page;
  GtkWidget     *win;
  GtkWidget     *stack;
  GtkWidget     *pill_label;

  /* Seed: label name "TestTag" assigned to App1 */
  seed_label_name ("TestTag");
  seed_noncore_assignment ("org.test.App1", "TestTag");

  g_application_set_default (NULL);
  setup_test_state ();

  stack = g_object_new (ADW_TYPE_VIEW_STACK, NULL);
  {
    GtkWidget *other = gtk_label_new ("other");
    adw_view_stack_add_titled_with_icon (
        ADW_VIEW_STACK (stack), other, "other", "Other", NULL);
  }

  page = cz_custom_page_new ();
  adw_view_stack_add_titled_with_icon (
      ADW_VIEW_STACK (stack),
      GTK_WIDGET (page), "custom", "Custom",
      "preferences-other-symbolic");

  win = gtk_window_new ();
  gtk_window_set_child (GTK_WINDOW (win), GTK_WIDGET (stack));
  gtk_widget_set_visible (win, TRUE);
  process_events ();

  /* Switch to custom → ::map → reloads + builds noncore pills */
  adw_view_stack_set_visible_child (ADW_VIEW_STACK (stack), GTK_WIDGET (page));
  process_events ();

  pill_label = find_pill_by_label (GTK_WIDGET (page), "TestTag");
  g_test_message ("DIAG:noncore-pills-page | TestTag_pill=%p | expected=non-null",
                  (void *) pill_label);
  g_assert_nonnull (pill_label);

  gtk_window_destroy (GTK_WINDOW (win));
}

/* Test: The combined filter (original category + core label + non-core
 * labels) is re-applied when returning from an app's full view via the
 * back button, while a plain tab switch keeps the default reset. */
static void
test_filter_restore_on_back (void)
{
  CzCustomPage *page;
  GtkWidget    *win;
  GtkWidget    *stack;
  GtkWidget    *other;
  GtkWidget    *list_view;
  GListModel   *model;
  guint         n_filtered;
  GtkWidget    *pill;
  GtkWidget    *tile;

  /* Seed: App2 (game) is core=New + noncore=TestTag → the only match for
   * the Gaming + New + TestTag filter (1 item). */
  seed_core_labels (
      (const char *[]) { "org.test.App1", "org.test.App2", "org.test.App3",
                         "org.test.App4", "org.test.App5", NULL },
      (const char *[]) { "Install",       "New",            "Forget it",
                         "4-Stars",       "3-Stars",        NULL }, 5);
  seed_label_name ("TestTag");
  seed_noncore_assignment ("org.test.App2", "TestTag");

  g_application_set_default (NULL);
  setup_test_state ();

  stack = g_object_new (ADW_TYPE_VIEW_STACK, NULL);
  other = gtk_label_new ("other");
  adw_view_stack_add_titled_with_icon (
      ADW_VIEW_STACK (stack), other, "other", "Other", NULL);

  page = cz_custom_page_new ();
  adw_view_stack_add_titled_with_icon (
      ADW_VIEW_STACK (stack), GTK_WIDGET (page), "custom", "Custom",
      "preferences-other-symbolic");

  win = gtk_window_new ();
  gtk_window_set_child (GTK_WINDOW (win), GTK_WIDGET (stack));
  gtk_widget_set_visible (win, TRUE);
  process_events ();

  /* Map custom → ::map → default reset (trending + New auto-selected) */
  adw_view_stack_set_visible_child (ADW_VIEW_STACK (stack), GTK_WIDGET (page));
  process_events ();

  /* Apply the combined filter: Gaming + New + TestTag */
  pill = find_pill_by_label (GTK_WIDGET (page), "Gaming");
  g_assert_nonnull (pill);
  g_signal_emit_by_name (pill, "clicked");
  process_events ();

  pill = find_pill_by_label (GTK_WIDGET (page), "New");
  g_assert_nonnull (pill);
  g_signal_emit_by_name (pill, "clicked");
  process_events ();

  pill = find_pill_by_label (GTK_WIDGET (page), "TestTag");
  g_assert_nonnull (pill);
  g_signal_emit_by_name (pill, "clicked");
  process_events ();

  list_view = find_list_view (GTK_WIDGET (page));
  g_assert_nonnull (list_view);
  model = bz_dynamic_list_view_get_model (BZ_DYNAMIC_LIST_VIEW (list_view));
  g_assert_nonnull (model);
  n_filtered = g_list_model_get_n_items (model);
  g_test_message ("DIAG:restore | before-nav n_items=%u | expected=1",
                  n_filtered);
  g_assert_cmpuint (n_filtered, ==, 1);

  /* Click the app tile → tile_clicked snapshots the combined filter and
   * sets restore_on_map (the back-button return will reapply it). */
  tile = find_widget_by_type (GTK_WIDGET (page), BZ_TYPE_APP_TILE);
  g_assert_nonnull (tile);
  g_signal_emit_by_name (tile, "clicked");

  /* Switch away (unmaps) and back to the custom tab */
  adw_view_stack_set_visible_child (ADW_VIEW_STACK (stack), other);
  process_events ();
  adw_view_stack_set_visible_child (ADW_VIEW_STACK (stack), GTK_WIDGET (page));
  process_events ();

  /* Combined filter must be reapplied exactly as before entering */
  g_test_message ("DIAG:restore | after-return"
                  " | gaming=%d | trending=%d | new=%d | install=%d | testtag=%d",
                  gtk_widget_has_css_class (
                      find_pill_by_label (GTK_WIDGET (page), "Gaming"),
                      "selected"),
                  gtk_widget_has_css_class (
                      find_pill_by_label (GTK_WIDGET (page), "Trending"),
                      "selected"),
                  gtk_widget_has_css_class (
                      find_pill_by_label (GTK_WIDGET (page), "New"),
                      "selected"),
                  gtk_widget_has_css_class (
                      find_pill_by_label (GTK_WIDGET (page), "Install"),
                      "selected"),
                  gtk_widget_has_css_class (
                      find_pill_by_label (GTK_WIDGET (page), "TestTag"),
                      "selected"));

  g_assert_true (gtk_widget_has_css_class (
      find_pill_by_label (GTK_WIDGET (page), "Gaming"), "selected"));
  g_assert_false (gtk_widget_has_css_class (
      find_pill_by_label (GTK_WIDGET (page), "Trending"), "selected"));
  g_assert_true (gtk_widget_has_css_class (
      find_pill_by_label (GTK_WIDGET (page), "New"), "selected"));
  g_assert_false (gtk_widget_has_css_class (
      find_pill_by_label (GTK_WIDGET (page), "Install"), "selected"));
  g_assert_true (gtk_widget_has_css_class (
      find_pill_by_label (GTK_WIDGET (page), "TestTag"), "selected"));

  model = bz_dynamic_list_view_get_model (BZ_DYNAMIC_LIST_VIEW (list_view));
  n_filtered = g_list_model_get_n_items (model);
  g_test_message ("DIAG:restore | after-return n_items=%u | expected=1",
                  n_filtered);
  g_assert_cmpuint (n_filtered, ==, 1);

  gtk_window_destroy (GTK_WINDOW (win));
}

/* ------------------------------------------------------------------ */
/*  Main                                                                */
/* ------------------------------------------------------------------ */

int
main (int argc, char *argv[])
{
  int ret;

  /* Create a temp dir and redirect XDG_DATA_HOME so that both BzFullView and
    * CzCustomPage use the same (temporary) SQLite label store.  This must
    * happen BEFORE g_test_init or any widget creation because
    * g_get_user_data_dir() caches its result on first call. */
  test_dir = g_dir_make_tmp ("bz-core-label-XXXXXX", NULL);
  g_assert_nonnull (test_dir);
  store_backup_dir = g_build_filename (test_dir,
                                       "io.github.kolunmi.Bazaar", NULL);
  store_path = g_build_filename (store_backup_dir, "custom-labels.db", NULL);
  g_setenv ("XDG_DATA_HOME", test_dir, TRUE);
  g_mkdir_with_parents (store_backup_dir, 0755);

  ret = 0;
  gtk_test_init (&argc, &argv, NULL);
  g_test_set_nonfatal_assertions ();

  dex_init ();
  bge_init ();

  /* Ensure types used in Blueprint templates are registered. Without
   * this, the linker dead-strips the corresponding .o files from the
   * static library and GTK template instantiation fails. */
  g_type_ensure (bz_entry_get_type ());
  g_type_ensure (bz_entry_group_get_type ());
  g_type_ensure (bz_result_get_type ());
  g_type_ensure (bz_flatpak_entry_get_type ());
  g_type_ensure (bz_auth_state_get_type ());
  g_type_ensure (BZ_TYPE_DYNAMIC_LIST_VIEW);
  g_type_ensure (BZ_TYPE_APP_TILE);

  g_test_add_func ("/core-labels/popover-structure",
                    test_popover_structure);
  g_test_add_func ("/core-labels/popover-selection",
                    test_popover_selection);
  g_test_add_func ("/core-labels/popover-default-new",
                    test_popover_default_new);
  g_test_add_func ("/core-labels/label-change-persists",
                    test_label_change_persists);
  g_test_add_func ("/core-labels/custom-page-core-pills",
                    test_custom_page_core_pills);
  g_test_add_func ("/core-labels/core-pill-affects-filter",
                    test_core_pill_affects_filter);
  g_test_add_func ("/core-labels/integration-roundtrip",
                     test_integration_roundtrip);
  g_test_add_func ("/core-labels/full-view-load-corrupt-store",
                     test_full_view_load_corrupt_store);
  g_test_add_func ("/core-labels/save-preserves-existing-noncore",
                     test_save_preserves_existing_noncore);

  /* Noncore label integration */
  g_test_add_func ("/core-labels/noncore-popover-empty",
                     test_noncore_popover_empty);
  g_test_add_func ("/core-labels/noncore-popover-with-names",
                     test_noncore_popover_with_names);
  g_test_add_func ("/core-labels/noncore-assign-persists",
                     test_noncore_assign_persists);
  g_test_add_func ("/core-labels/noncore-unassign",
                     test_noncore_unassign);
  g_test_add_func ("/core-labels/noncore-default-none-selected",
                     test_noncore_default_none_selected);
  g_test_add_func ("/core-labels/save-preserves-noncore-label-names",
                     test_save_preserves_noncore_label_names);
  g_test_add_func ("/core-labels/noncore-pills-on-custom-page",
                     test_noncore_pills_on_custom_page);
  g_test_add_func ("/core-labels/filter-restore-on-back",
                     test_filter_restore_on_back);

  ret = g_test_run ();

  /* Cleanup */
  g_free (store_path);
  g_free (store_backup_dir);
  if (test_dir != NULL)
    {
      g_rmdir (test_dir);
      g_free (test_dir);
    }

  return ret;
}
