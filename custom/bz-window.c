/* bz-window.c
 *
 * Copyright 2025 Adam Masciola
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// This file is an utter mess
#include "config.h"

#include <gio/gio.h>

#include <glib/gi18n.h>

#include "bz-addons-dialog.h"
#include "bz-application.h"
#include "bz-curated-view.h"
#include "bz-entry-group-util.h"
#include "bz-entry-group.h"
#include "bz-env.h"
#include "bz-error.h"
#include "bz-flathub-page.h"
#include "bz-flatpak-entry.h"
#include "bz-full-view.h"
#include "bz-hooks.h"
#include "bz-io.h"
#include "bz-label-store.h"
#include "bz-library-page.h"
#include "bz-progress-bar.h"
#include "bz-screenshot-page.h"
#include "bz-search-page.h"
#include "bz-template-callbacks.h"
#include "bz-transaction-dialog.h"
#include "bz-transaction-manager.h"
#include "bz-user-data-page.h"
#include "bz-util.h"
#include "bz-window.h"
#include "cz-app-report.h"
#include "cz-custom-label-store.h"

enum
{
  NAV_VIEW_ROOT = 0,
  NAV_VIEW_LABELS = 1,
};

struct _BzWindow
{
  AdwApplicationWindow parent_instance;

  BzStateInfo *state;

  GtkEventController *key_controller;

  BzScreenshotPage *screenshot_page;

  gboolean breakpoint_applied;

  /* Template widgets */
  AdwNavigationView *navigation_view;
  BzFullView        *full_view;
  BzSearchPage      *search_page;
  BzLibraryPage     *library_page;
  AdwToastOverlay   *toasts;
  AdwViewStack      *main_view_stack;
  GtkStack          *main_stack;
  GtkOverlay        *window_overlay;
  GtkButton          *custom_nav_button;

  /* Non-core label management (custom tab hamburger) */
  GHashTable              *local_noncore_names; /* set of noncore label names */
  BzLabelStore            *label_store;
  GtkWindow               *custom_nav_popup;
  gboolean                 popup_visible;
  gboolean                 rebuilding_popup;
  guint                    current_nav_view; /* 0 = root menu, 1 = label manager */
  BzNoncoreNameAddedFunc   noncore_name_added_cb;
  BzNoncoreNameRemovedFunc noncore_name_removed_cb;
  gpointer                 noncore_name_cb_data;

  /* Empty DB recovery */
  gboolean   db_empty;
  char      *db_path;
};

G_DEFINE_FINAL_TYPE (BzWindow, bz_window, ADW_TYPE_APPLICATION_WINDOW)

enum
{
  PROP_0,

  PROP_STATE,
  PROP_COMPACT,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

BZ_DEFINE_DATA (
    transact,
    Transact,
    {
      GWeakRef     *self;
      BzEntry      *entry;
      BzEntryGroup *group;
      gboolean      remove;
      gboolean      auto_confirm;
      GtkWidget    *source;
    },
    BZ_RELEASE_DATA (self, bz_weak_release);
    BZ_RELEASE_DATA (entry, g_object_unref);
    BZ_RELEASE_DATA (group, g_object_unref);
    BZ_RELEASE_DATA (source, g_object_unref))

static DexFuture *
transact_fiber (TransactData *data);

BZ_DEFINE_DATA (
    bulk_install,
    BulkInstall,
    {
      GWeakRef   *self;
      GListModel *groups;
    },
    BZ_RELEASE_DATA (self, bz_weak_release);
    BZ_RELEASE_DATA (groups, g_object_unref))

static DexFuture *
bulk_install_fiber (BulkInstallData *data);

static DexFuture *
transact (BzEntry   *entry,
          gboolean   remove,
          GtkWidget *source);

static void
try_transact (BzWindow     *self,
              BzEntry      *entry,
              BzEntryGroup *group,
              gboolean      remove,
              gboolean      auto_confirm,
              GtkWidget    *source);

static void
search (BzWindow   *self,
        const char *text);

static void
bulk_install (BzWindow *self,
              BzEntry **installs,
              guint     n_installs);

static void
set_page (BzWindow *self);

static void
emit_hook_disown (BzWindow     *self,
                  BzHookSignal  signal,
                  BzEntryGroup *group);

/* ------------------------------------------------------------------ */
/*  GObject lifecycle                                                    */
/* ------------------------------------------------------------------ */

static void
bz_label_store_destroy (gpointer data)
{
  bz_label_store_close (data);
}

static void
bz_window_dispose (GObject *object)
{
  BzWindow *self = BZ_WINDOW (object);

  g_clear_object (&self->state);
  g_clear_pointer (&self->local_noncore_names, g_hash_table_unref);
  g_clear_pointer (&self->label_store, bz_label_store_destroy);
  if (self->custom_nav_popup != NULL)
    {
      gtk_widget_unparent (GTK_WIDGET (self->custom_nav_popup));
      self->custom_nav_popup = NULL;
    }

  G_OBJECT_CLASS (bz_window_parent_class)->dispose (object);
}

static void
bz_window_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
  BzWindow *self = BZ_WINDOW (object);

  switch (prop_id)
    {
    case PROP_STATE:
      g_value_set_object (value, self->state);
      break;
    case PROP_COMPACT:
      g_value_set_boolean (value, self->breakpoint_applied);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_window_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
  // BzWindow *self = BZ_WINDOW (object);

  switch (prop_id)
    {
    case PROP_STATE:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static char *
list_length (gpointer    object,
             GListModel *model)
{
  if (model == NULL)
    return g_strdup (0);

  return g_strdup_printf ("%u", g_list_model_get_n_items (model));
}

static void
update_cb (BzWindow   *self,
           GListModel *entries,
           GtkWidget  *widget)
{
  g_autoptr (BzTransaction) transaction  = NULL;
  guint                n_updates         = 0;
  g_autofree BzEntry **updates_buf       = NULL;
  GListModel          *available_updates = NULL;

  g_return_if_fail (BZ_IS_WINDOW (self));
  g_return_if_fail (G_IS_LIST_MODEL (entries));

  n_updates = g_list_model_get_n_items (entries);
  if (n_updates == 0)
    return;

  updates_buf = g_malloc_n (n_updates, sizeof (*updates_buf));
  for (guint i = 0; i < n_updates; i++)
    updates_buf[i] = g_list_model_get_item (entries, i);

  transaction = bz_transaction_new_full (
      NULL, 0,
      updates_buf, n_updates,
      NULL, 0);

  dex_future_disown (bz_transaction_manager_add (
      bz_state_info_get_transaction_manager (self->state),
      transaction));

  available_updates = bz_state_info_get_available_updates (self->state);
  if (G_IS_LIST_STORE (available_updates))
    {
      GListStore *store       = G_LIST_STORE (available_updates);
      guint       n_available = g_list_model_get_n_items (available_updates);

      for (guint i = n_available; i > 0; i--)
        {
          guint current_size                  = 0;
          guint idx                           = 0;
          g_autoptr (BzEntry) available_entry = NULL;
          const char *available_id            = NULL;

          idx          = i - 1;
          current_size = g_list_model_get_n_items (available_updates);

          if (idx >= current_size)
            continue;

          available_entry = g_list_model_get_item (available_updates, idx);
          available_id    = bz_entry_get_id (available_entry);

          for (guint j = 0; j < n_updates; j++)
            {
              if (g_strcmp0 (available_id, bz_entry_get_id (updates_buf[j])) == 0)
                {
                  g_list_store_remove (store, idx);
                  break;
                }
            }
        }
    }

  g_object_notify (G_OBJECT (self->state), "available-updates");

  for (guint i = 0; i < n_updates; i++)
    g_object_unref (updates_buf[i]);
}

void
bz_window_show_app_id (BzWindow   *self,
                       const char *app_id)
{
  g_autoptr (BzEntryGroup) group = NULL;

  g_return_if_fail (BZ_IS_WINDOW (self));
  g_return_if_fail (app_id != NULL);

  group = bz_application_map_factory_convert_one (
      bz_state_info_get_application_factory (self->state),
      gtk_string_object_new (app_id));

  if (group != NULL)
    bz_window_show_group (self, group);
}

static void
page_toggled_cb (BzWindow       *self,
                 GParamSpec     *pspec,
                 AdwToggleGroup *toggles)
{
  set_page (self);
}

static void
browse_flathub_cb (BzWindow      *self,
                   BzCuratedView *widget)
{
  adw_view_stack_set_visible_child_name (self->main_view_stack, "flathub");
}

static void
open_search_cb (BzWindow     *self,
                BzSearchPage *widget)
{
  adw_view_stack_set_visible_child_name (self->main_view_stack, "search");
}

static void
breakpoint_apply_cb (BzWindow      *self,
                     AdwBreakpoint *breakpoint)
{
  self->breakpoint_applied = TRUE;

  gtk_widget_add_css_class (GTK_WIDGET (self), "narrow");
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_COMPACT]);
}

static void
breakpoint_unapply_cb (BzWindow      *self,
                       AdwBreakpoint *breakpoint)
{
  self->breakpoint_applied = FALSE;

  gtk_widget_remove_css_class (GTK_WIDGET (self), "narrow");
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_COMPACT]);
}

static void
sync_cb (BzWindow  *self,
         GtkButton *button)
{
  g_action_group_activate_action (
      G_ACTION_GROUP (g_application_get_default ()),
      "sync-remotes", NULL);
}

static void
transactions_clear_cb (BzWindow  *self,
                       GtkButton *button)
{
  bz_transaction_manager_clear_finished (
      bz_state_info_get_transaction_manager (self->state));
}

static void
action_escape (GtkWidget  *widget,
               const char *action_name,
               GVariant   *parameter)
{
  BzWindow   *self    = BZ_WINDOW (widget);
  GListModel *stack   = NULL;
  guint       n_pages = 0;

  if (self->screenshot_page != NULL)
    {
      if (!bz_screenshot_page_is_closing (self->screenshot_page))
        {
          bz_screenshot_page_close (self->screenshot_page);
          return;
        }
    }

  stack   = adw_navigation_view_get_navigation_stack (self->navigation_view);
  n_pages = g_list_model_get_n_items (stack);

  adw_navigation_view_pop (self->navigation_view);
  if (n_pages <= 2)
    set_page (self);
}

static char *
format_progress (gpointer object,
                 double   value)
{
  return g_strdup_printf ("%.0f%%", 100.0 * value);
}

static char *
format_title (gpointer    object,
              const char *title)
{
  if (title == NULL || *title == '\0' || g_strcmp0 (title, _ ("Bazaar")) == 0)
    return g_strdup (_ ("Bazaar"));
  /* Translators: %s is the title of the current page */
  return g_strdup_printf (_ ("Bazaar — %s"), title);
}

static BzEntryGroup *
resolve_group_from_parameter (BzWindow *self,
                              GVariant *parameter,
                              gboolean *auto_confirm)
{
  const char *id = NULL;

  g_variant_get (parameter, "(&sb)", &id, auto_confirm);

  return bz_application_map_factory_convert_one (
      bz_state_info_get_application_factory (self->state),
      gtk_string_object_new (id));
}

static void
action_install_group (GtkWidget  *widget,
                      const char *action_name,
                      GVariant   *parameter)
{
  BzWindow *self                 = BZ_WINDOW (widget);
  g_autoptr (BzEntryGroup) group = NULL;
  gboolean auto_confirm          = FALSE;

  group = resolve_group_from_parameter (self, parameter, &auto_confirm);
  if (group != NULL)
    try_transact (self, NULL, group, FALSE, auto_confirm, NULL);
}

static void
action_remove_group (GtkWidget  *widget,
                     const char *action_name,
                     GVariant   *parameter)
{
  BzWindow *self                 = BZ_WINDOW (widget);
  g_autoptr (BzEntryGroup) group = NULL;
  gboolean auto_confirm          = FALSE;

  group = resolve_group_from_parameter (self, parameter, &auto_confirm);
  if (group != NULL)
    try_transact (self, NULL, group, TRUE, auto_confirm, NULL);
}

static void
action_cancel_group (GtkWidget  *widget,
                     const char *action_name,
                     GVariant   *parameter)
{
  BzWindow             *self     = BZ_WINDOW (widget);
  const char           *id       = NULL;
  BzTransactionManager *manager  = NULL;
  BzBackend            *backend  = NULL;
  GListModel           *trackers = NULL;
  guint                 n_items  = 0;

  id      = g_variant_get_string (parameter, NULL);
  manager = bz_state_info_get_transaction_manager (self->state);
  if (manager == NULL)
    return;

  backend = bz_state_info_get_backend (self->state);
  if (backend == NULL)
    return;

  trackers = bz_transaction_manager_get_all_trackers (manager);
  n_items  = g_list_model_get_n_items (trackers);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr (BzTransactionEntryTracker) tracker = NULL;
      BzEntry    *entry                             = NULL;
      const char *entry_id                          = NULL;

      tracker = g_list_model_get_item (trackers, i);
      entry   = bz_transaction_entry_tracker_get_entry (tracker);
      if (entry == NULL)
        continue;

      entry_id = bz_entry_get_id (entry);
      if (g_strcmp0 (entry_id, id) == 0)
        {
          if (bz_backend_cancel_task_for_entry (backend, entry))
            g_object_set (tracker, "status", BZ_TRANSACTION_ENTRY_STATUS_CANCELLED, NULL);
          break;
        }
    }
}

static void
action_show_group (GtkWidget  *widget,
                   const char *action_name,
                   GVariant   *parameter)
{
  BzWindow   *self               = BZ_WINDOW (widget);
  const char *id                 = NULL;
  g_autoptr (BzEntryGroup) group = NULL;

  id    = g_variant_get_string (parameter, NULL);
  group = bz_application_map_factory_convert_one (
      bz_state_info_get_application_factory (self->state),
      gtk_string_object_new (id));

  if (group == NULL)
    return;

  if (bz_entry_group_is_addon (group))
    {
      AdwDialog *dialog = NULL;

      dialog = bz_addons_dialog_new_single (group);
      adw_dialog_present (dialog, GTK_WIDGET (self));
    }
  else
    bz_window_show_group (self, group);
}

static void
action_addons_group (GtkWidget  *widget,
                     const char *action_name,
                     GVariant   *parameter)
{
  BzWindow   *self               = BZ_WINDOW (widget);
  const char *id                 = NULL;
  g_autoptr (BzEntryGroup) group = NULL;
  AdwDialog *addons_dialog       = NULL;

  id    = g_variant_get_string (parameter, NULL);
  group = bz_application_map_factory_convert_one (
      bz_state_info_get_application_factory (self->state),
      gtk_string_object_new (id));

  if (group == NULL)
    return;

  addons_dialog = bz_addons_dialog_new (group);
  adw_dialog_present (addons_dialog, GTK_WIDGET (self));
}

static void
action_bulk_install (GtkWidget  *widget,
                     const char *action_name,
                     GVariant   *parameter)
{
  BzWindow *self                = BZ_WINDOW (widget);
  g_autoptr (GListStore) ids    = NULL;
  g_autoptr (GListModel) groups = NULL;
  GVariantIter iter             = { 0 };
  const char  *id               = NULL;

  ids = g_list_store_new (GTK_TYPE_STRING_OBJECT);

  g_variant_iter_init (&iter, parameter);
  while (g_variant_iter_next (&iter, "&s", &id))
    {
      g_autoptr (GtkStringObject) string = gtk_string_object_new (id);
      g_list_store_append (ids, string);
    }

  groups = bz_application_map_factory_generate (
      bz_state_info_get_application_factory (self->state),
      G_LIST_MODEL (ids));

  if (groups != NULL && g_list_model_get_n_items (groups) > 0)
    bz_window_bulk_install (self, groups);
}

static void
action_user_data (GtkWidget  *widget,
                  const char *action_name,
                  GVariant   *parameter)
{
  BzWindow          *self           = BZ_WINDOW (widget);
  AdwNavigationPage *user_data_page = NULL;

  user_data_page = ADW_NAVIGATION_PAGE (bz_user_data_page_new (self->state));
  adw_navigation_view_push (self->navigation_view, user_data_page);
}

static void
action_open_flathub_page (GtkWidget  *widget,
                          const char *action_name,
                          GVariant   *parameter)
{
  BzWindow *self = BZ_WINDOW (widget);

  adw_navigation_view_pop_to_tag (self->navigation_view, "main");
  adw_view_stack_set_visible_child_name (self->main_view_stack, "flathub");
}

static void
action_open_library (GtkWidget  *widget,
                     const char *action_name,
                     GVariant   *parameter)
{
  BzWindow *self = BZ_WINDOW (widget);

  adw_navigation_view_pop_to_tag (self->navigation_view, "main");
  adw_view_stack_set_visible_child_name (self->main_view_stack, "installed");
  bz_library_page_reset_search (self->library_page);
}

static DexFuture *
launch_group_fiber (BzEntryGroup *group)
{
  g_autoptr (GError) local_error = NULL;
  g_autoptr (GListStore) store   = NULL;
  GtkWidget   *window            = NULL;
  BzStateInfo *state             = NULL;

  state  = bz_state_info_get_default ();
  window = GTK_WIDGET (gtk_application_get_active_window (
      GTK_APPLICATION (g_application_get_default ())));

  store = dex_await_object (
      bz_entry_group_dup_all_into_store (group), &local_error);
  if (store == NULL)
    {
      if (window != NULL)
        bz_show_error_for_widget (window, _ ("Failed to launch application"), local_error->message);
      return dex_future_new_for_error (g_steal_pointer (&local_error));
    }

  for (guint i = 0; i < g_list_model_get_n_items (G_LIST_MODEL (store)); i++)
    {
      g_autoptr (BzEntry) entry = NULL;
      const char *ref           = NULL;
      gboolean    result        = FALSE;

      entry = g_list_model_get_item (G_LIST_MODEL (store), i);

      if (!BZ_IS_FLATPAK_ENTRY (entry) || !bz_entry_is_installed (entry))
        continue;

      ref = bz_flatpak_entry_get_addon_extension_of_ref (BZ_FLATPAK_ENTRY (entry));
      if (ref != NULL)
        {
          g_auto (GStrv) parts             = NULL;
          BzApplicationMapFactory *factory = NULL;
          g_autoptr (BzEntryGroup) parent  = NULL;

          parts = g_strsplit (ref, "/", -1);
          if (parts[0] != NULL && parts[1] != NULL)
            {
              factory = bz_state_info_get_application_factory (state);
              parent  = bz_application_map_factory_convert_one (
                  factory, gtk_string_object_new (parts[1]));
              if (parent != NULL)
                return launch_group_fiber (parent);
            }
        }

      result = bz_flatpak_entry_launch (
          BZ_FLATPAK_ENTRY (entry),
          BZ_FLATPAK_INSTANCE (bz_state_info_get_backend (state)),
          &local_error);
      if (!result && window != NULL)
        bz_show_error_for_widget (window, _ ("Failed to launch application"), local_error->message);
      return result ? dex_future_new_true () : dex_future_new_for_error (g_steal_pointer (&local_error));
    }

  return dex_future_new_false ();
}

static void
action_launch_group (GtkWidget  *widget,
                     const char *action_name,
                     GVariant   *parameter)
{
  BzWindow   *self               = BZ_WINDOW (widget);
  const char *id                 = NULL;
  g_autoptr (BzEntryGroup) group = NULL;

  id    = g_variant_get_string (parameter, NULL);
  group = bz_application_map_factory_convert_one (
      bz_state_info_get_application_factory (self->state),
      gtk_string_object_new (id));

  if (group == NULL)
    return;

  dex_future_disown (dex_scheduler_spawn (
      dex_scheduler_get_default (),
      bz_get_dex_stack_size (),
      (DexFiberFunc) launch_group_fiber,
      g_object_ref (group),
      g_object_unref));
}

extern GResource *bz_get_resource (void);

static void
bz_window_class_init (BzWindowClass *klass)
{
  /* Reference the compiled-in GResource to prevent dead-stripping
   * when BzWindow is linked from a static library.  The call itself
   * is harmless — it returns the already-registered resource. */
  (void) bz_get_resource ();

  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_window_dispose;
  object_class->get_property = bz_window_get_property;
  object_class->set_property = bz_window_set_property;

  props[PROP_STATE] =
      g_param_spec_object (
          "state",
          NULL, NULL,
          BZ_TYPE_STATE_INFO,
          G_PARAM_READABLE);

  props[PROP_COMPACT] =
      g_param_spec_boolean (
          "compact",
          NULL, NULL, FALSE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_type_ensure (BZ_TYPE_SEARCH_PAGE);
  g_type_ensure (BZ_TYPE_PROGRESS_BAR);
  g_type_ensure (BZ_TYPE_CURATED_VIEW);
  g_type_ensure (BZ_TYPE_FULL_VIEW);
  g_type_ensure (BZ_TYPE_LIBRARY_PAGE);
  g_type_ensure (BZ_TYPE_FLATHUB_PAGE);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-window.ui");
  bz_widget_class_bind_all_util_callbacks (widget_class);

  gtk_widget_class_bind_template_child (widget_class, BzWindow, navigation_view);
  gtk_widget_class_bind_template_child (widget_class, BzWindow, full_view);
  gtk_widget_class_bind_template_child (widget_class, BzWindow, toasts);
  gtk_widget_class_bind_template_child (widget_class, BzWindow, search_page);
  gtk_widget_class_bind_template_child (widget_class, BzWindow, library_page);
  gtk_widget_class_bind_template_child (widget_class, BzWindow, main_view_stack);
  gtk_widget_class_bind_template_child (widget_class, BzWindow, main_stack);
  gtk_widget_class_bind_template_child (widget_class, BzWindow, window_overlay);
  gtk_widget_class_bind_template_child (widget_class, BzWindow, custom_nav_button);
  gtk_widget_class_bind_template_callback (widget_class, list_length);
  gtk_widget_class_bind_template_callback (widget_class, update_cb);
  gtk_widget_class_bind_template_callback (widget_class, page_toggled_cb);
  gtk_widget_class_bind_template_callback (widget_class, breakpoint_apply_cb);
  gtk_widget_class_bind_template_callback (widget_class, breakpoint_unapply_cb);
  gtk_widget_class_bind_template_callback (widget_class, sync_cb);
  gtk_widget_class_bind_template_callback (widget_class, transactions_clear_cb);
  gtk_widget_class_bind_template_callback (widget_class, browse_flathub_cb);
  gtk_widget_class_bind_template_callback (widget_class, open_search_cb);
  gtk_widget_class_bind_template_callback (widget_class, format_progress);
  gtk_widget_class_bind_template_callback (widget_class, format_title);

  gtk_widget_class_install_action (widget_class, "escape", NULL, action_escape);
  gtk_widget_class_install_action (widget_class, "window.user-data", NULL, action_user_data);
  gtk_widget_class_install_action (widget_class, "window.open-library", NULL, action_open_library);
  gtk_widget_class_install_action (widget_class, "window.open-flathub-page", NULL, action_open_flathub_page);

  gtk_widget_class_install_action (widget_class, "window.install-group", "(sb)", action_install_group);
  gtk_widget_class_install_action (widget_class, "window.remove-group", "(sb)", action_remove_group);
  gtk_widget_class_install_action (widget_class, "window.cancel-group", "s", action_cancel_group);
  gtk_widget_class_install_action (widget_class, "window.show-group", "s", action_show_group);
  gtk_widget_class_install_action (widget_class, "window.addons-group", "s", action_addons_group);
  gtk_widget_class_install_action (widget_class, "window.bulk-install", NULL, action_bulk_install);
  gtk_widget_class_install_action (widget_class, "window.launch-group", "s", action_launch_group);

  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_d, GDK_CONTROL_MASK, "window.open-library", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_w, GDK_CONTROL_MASK, "window.close", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_e, GDK_CONTROL_MASK, "window.open-flathub-page", NULL);
}

static gboolean
key_pressed (BzWindow              *self,
             guint                  keyval,
             guint                  keycode,
             GdkModifierType        state,
             GtkEventControllerKey *controller)
{
  gunichar    unichar            = 0;
  char        buf[32]            = { 0 };
  const char *visible_child_name = NULL;
  gboolean    was_deeper         = FALSE;

  /* Ignore if this is a modifier-shortcut of some sort */
  if (state & ~(GDK_NO_MODIFIER_MASK | GDK_SHIFT_MASK))
    return FALSE;

  if (self->screenshot_page != NULL)
    return FALSE;

  unichar = gdk_keyval_to_unicode (keyval);
  if (unichar == 0 || !g_unichar_isgraph (unichar))
    return FALSE;
  g_unichar_to_utf8 (unichar, buf);

  was_deeper = g_list_model_get_n_items (
                   adw_navigation_view_get_navigation_stack (self->navigation_view)) > 1;

  adw_navigation_view_pop_to_tag (self->navigation_view, "main");

  visible_child_name = adw_view_stack_get_visible_child_name (self->main_view_stack);
  if (!was_deeper && g_strcmp0 (visible_child_name, "installed") == 0)
    return bz_library_page_ensure_active (self->library_page, buf);
  else
    {
      adw_view_stack_set_visible_child_name (self->main_view_stack, "search");
      return bz_search_page_ensure_active (self->search_page, buf);
    }
}

/* ------------------------------------------------------------------ */
/*  Custom nav (non-core label management)                              */
/* ------------------------------------------------------------------ */

static void
custom_nav_load_names (BzWindow *self)
{
  char **names;
  guint  i;

  if (self->label_store == NULL)
    return;

  names = bz_label_store_get_all_label_names (self->label_store);
  if (names == NULL)
    return;

  for (i = 0; names[i] != NULL; i++)
    g_hash_table_add (self->local_noncore_names, g_strdup (names[i]));
  g_strfreev (names);
}

/* Forward declarations */
/* Forward declarations */

static void on_custom_nav_add_clicked (GtkEntry *, gpointer);
static void rebuild_custom_nav_popup (BzWindow *);
void        bz_full_view_update_noncore_label_names (BzFullView *, GPtrArray *);

/* Check how many apps have a given noncore label assigned.
 * Returns 0 if store is unavailable or label isn't used. */
static guint
noncore_label_assignment_count (BzWindow   *self,
                                const char *label)
{
  if (self->label_store == NULL)
    return 0;

  return bz_label_store_count_noncore_label (self->label_store, label);
}

/* Response callback for the delete-confirmation dialog. */
static void
on_delete_confirm_response (GObject      *source,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  BzWindow       *self   = BZ_WINDOW (user_data);
  GtkAlertDialog *dialog = GTK_ALERT_DIALOG (source);
  int             response;
  const char     *name;

  response = gtk_alert_dialog_choose_finish (dialog, result, NULL);
  if (response != 1) /* "_Delete anyway" button index */
    return;

  name = (const char *) g_object_get_data (G_OBJECT (dialog), "noncore-name");
  if (name == NULL)
    return;

  g_hash_table_remove (self->local_noncore_names, name);
  bz_label_store_remove_label_name (self->label_store, name, NULL);

  /* Notify custom-label store so pills rebuild in real time */
  if (self->noncore_name_removed_cb != NULL)
    self->noncore_name_removed_cb (name, self->noncore_name_cb_data);

  /* Sync the full-view so its noncore popover reflects the change.
     Use the in-memory label list from the custom label store to avoid
     SQLite cross-connection visibility issues. */
  if (BZ_IS_FULL_VIEW (self->full_view) && self->noncore_name_cb_data != NULL)
    {
      CzCustomLabelStore *store     = CZ_CUSTOM_LABEL_STORE (self->noncore_name_cb_data);
      GPtrArray          *names_ptr = cz_custom_label_store_get_all_noncore_label_names (store);
      if (names_ptr != NULL)
        {
          bz_full_view_update_custom_label_names (self->full_view, names_ptr);
        }
    }

  rebuild_custom_nav_popup (self);

  g_object_set_data (G_OBJECT (dialog), "noncore-name", NULL);
}

static void
on_custom_nav_delete_clicked (GtkButton *button,
                              gpointer   user_data)
{
  BzWindow   *self = BZ_WINDOW (user_data);
  const char *name;
  guint       n;

  name = (const char *) g_object_get_data (G_OBJECT (button), "noncore-name");
  if (name == NULL)
    return;

  n = noncore_label_assignment_count (self, name);

  if (n > 0)
    {
      const char     *btn[] = { "_Cancel", "_Delete anyway", NULL };
      char           *msg;
      GtkAlertDialog *dialog;

      msg = g_strdup_printf (
          "The label \"%s\" is assigned to %u app(s). "
          "Deleting it will remove this label from those apps.",
          name, n);
      dialog = gtk_alert_dialog_new ("%s", msg);
      g_free (msg);

      g_object_set_data_full (G_OBJECT (dialog), "noncore-name",
                              g_strdup (name), g_free);

      gtk_alert_dialog_set_buttons (dialog, btn);
      gtk_alert_dialog_set_cancel_button (dialog, 0);

      gtk_alert_dialog_choose (dialog,
                               GTK_WINDOW (self),
                               NULL,
                               on_delete_confirm_response,
                               self);
      return;
    }

  /* No apps use this label — delete directly.
     Save a copy of name before removing from hash, because
     local_noncore_names uses g_free as key destroy, so
     g_hash_table_remove frees the key pointer. */
  {
    g_autofree char *saved_name = g_strdup (name);

    g_hash_table_remove (self->local_noncore_names, name);

    {
      g_autoptr (GError) err = NULL;
      if (!bz_label_store_remove_label_name (self->label_store, saved_name, &err))
        {
          g_critical ("Failed to remove label name from store: %s", err->message);
          g_hash_table_add (self->local_noncore_names, g_strdup (saved_name));
          return;
        }
    }

    /* Notify custom-label store so pills rebuild in real time */
    if (self->noncore_name_removed_cb != NULL)
      self->noncore_name_removed_cb (saved_name, self->noncore_name_cb_data);

    /* Sync the full-view so its noncore popover reflects the change. */
    if (BZ_IS_FULL_VIEW (self->full_view) && self->noncore_name_cb_data != NULL)
      {
        CzCustomLabelStore *store     = CZ_CUSTOM_LABEL_STORE (self->noncore_name_cb_data);
        GPtrArray          *names_ptr = cz_custom_label_store_get_all_noncore_label_names (store);
        if (names_ptr != NULL)
          bz_full_view_update_custom_label_names (self->full_view, names_ptr);
      }

    rebuild_custom_nav_popup (self);
  }
}

static void
on_custom_nav_add_btn_clicked (GtkButton *button,
                               gpointer   user_data)
{
  GtkWidget *entry = g_object_get_data (G_OBJECT (button), "noncore-entry");
  if (entry != NULL)
    on_custom_nav_add_clicked (GTK_ENTRY (entry), user_data);
}

static void
on_custom_nav_add_clicked (GtkEntry *entry,
                           gpointer  user_data)
{
  BzWindow   *self = BZ_WINDOW (user_data);
  const char *text;

  text = gtk_editable_get_text (GTK_EDITABLE (entry));
  if (text == NULL || *text == '\0')
    return;

  if (g_hash_table_contains (self->local_noncore_names, text))
    return;

  /* Copy the text before clearing the entry, because
     gtk_editable_set_text may invalidate the pointer returned by
     gtk_editable_get_text. */
  {
    char *label_name = g_strdup (text);

    g_hash_table_add (self->local_noncore_names, g_strdup (label_name));
    gtk_editable_set_text (GTK_EDITABLE (entry), "");
    {
      g_autoptr (GError) err = NULL;
      if (!bz_label_store_add_label_name (self->label_store, label_name, &err))
        {
          g_critical ("Failed to add label name to store: %s", err->message);
          g_hash_table_remove (self->local_noncore_names, label_name);
          g_free (label_name);
          return;
        }
    }

    /* Notify custom-label store so pills rebuild in real time */
    if (self->noncore_name_added_cb != NULL)
      self->noncore_name_added_cb (label_name, self->noncore_name_cb_data);

    /* Sync the full-view so its noncore popover reflects the new name.
       Use the in-memory label list from the custom label store to avoid
       SQLite cross-connection visibility issues. */
    if (BZ_IS_FULL_VIEW (self->full_view) && self->noncore_name_cb_data != NULL)
      {
        CzCustomLabelStore *store     = CZ_CUSTOM_LABEL_STORE (self->noncore_name_cb_data);
        GPtrArray          *names_ptr = cz_custom_label_store_get_all_noncore_label_names (store);
        if (names_ptr != NULL)
          {
            bz_full_view_update_custom_label_names (self->full_view, names_ptr);
          }
      }

    /* Rebuild the popup content to show the new name in the list */
    rebuild_custom_nav_popup (self);

    /* Re-focus the entry widget */
    {
      GtkWidget *child = gtk_window_get_child (self->custom_nav_popup);
      if (child != NULL)
        {
          GtkWidget *e = gtk_widget_get_first_child (child);
          if (e != NULL)
            gtk_widget_grab_focus (e);
        }
    }

    g_free (label_name);
  }
}

static gint
compare_names (gconstpointer a, gconstpointer b)
{
  return g_strcmp0 (*(const char **) a, *(const char **) b);
}

/* ---- Nav root menu -------------------------------------------------- */

static void
on_nav_labels_clicked (GtkButton *button,
                       BzWindow  *self)
{
  (void) button;
  self->current_nav_view = NAV_VIEW_LABELS;
  rebuild_custom_nav_popup (self);
}

static void
on_nav_back_clicked (GtkButton *button,
                     BzWindow  *self)
{
  (void) button;
  self->current_nav_view = NAV_VIEW_ROOT;
  rebuild_custom_nav_popup (self);
}

static void
on_nav_report_clicked (GtkButton *button,
                       BzWindow  *self)
{
  g_autoptr (GError) err = NULL;
  char              *path = NULL;
  char              *reports_dir = NULL;

  (void) button;

  if (self->label_store == NULL)
    {
      adw_toast_overlay_add_toast (
          self->toasts, adw_toast_new (_ ("Label store unavailable")));
      self->popup_visible = FALSE;
      gtk_widget_set_visible (GTK_WIDGET (self->custom_nav_popup), FALSE);
      return;
    }

  path = cz_app_report_suggest_path (self->db_path);
  if (!cz_app_report_generate (self->label_store, self->state, path, &err))
    {
      adw_toast_overlay_add_toast (
          self->toasts, adw_toast_new (err->message));
      g_free (path);
      self->popup_visible = FALSE;
      gtk_widget_set_visible (GTK_WIDGET (self->custom_nav_popup), FALSE);
      return;
    }

  reports_dir = g_path_get_dirname (path);
  cz_app_report_prune (reports_dir, CZ_APP_REPORT_RETENTION);
  g_free (reports_dir);
  g_free (path);

  adw_toast_overlay_add_toast (
      self->toasts, adw_toast_new (_ ("App report exported")));
  self->popup_visible = FALSE;
  gtk_widget_set_visible (GTK_WIDGET (self->custom_nav_popup), FALSE);
}

static void
rebuild_custom_nav_root_view (BzWindow *self)
{
  GtkWidget *box;
  GtkWidget *btn;
  GtkWidget *item_label;

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_margin_start (box, 4);
  gtk_widget_set_margin_end (box, 4);
  gtk_widget_set_margin_top (box, 4);
  gtk_widget_set_margin_bottom (box, 4);
  gtk_widget_set_size_request (box, 280, -1);

  /* Label creator */
  btn = gtk_button_new ();
  gtk_widget_add_css_class (btn, "menu-item");
  item_label = gtk_label_new (_ ("Label creator"));
  gtk_widget_set_halign (item_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand (item_label, TRUE);
  gtk_button_set_child (GTK_BUTTON (btn), item_label);
  g_signal_connect (btn, "clicked",
                    G_CALLBACK (on_nav_labels_clicked), self);
  gtk_box_append (GTK_BOX (box), btn);

  /* App Report */
  btn = gtk_button_new ();
  gtk_widget_add_css_class (btn, "menu-item");
  item_label = gtk_label_new (_ ("App Report"));
  gtk_widget_set_halign (item_label, GTK_ALIGN_START);
  gtk_widget_set_hexpand (item_label, TRUE);
  gtk_button_set_child (GTK_BUTTON (btn), item_label);
  g_signal_connect (btn, "clicked",
                    G_CALLBACK (on_nav_report_clicked), self);
  gtk_box_append (GTK_BOX (box), btn);

  self->rebuilding_popup = TRUE;
  gtk_window_set_child (self->custom_nav_popup, box);
  self->rebuilding_popup = FALSE;
}

/* ---- Label name manager --------------------------------------------- */

static void
rebuild_custom_nav_labels_view (BzWindow *self)
{
  GtkWidget *box;
  GtkWidget *back_btn;
  GtkWidget *back_label;
  GtkWidget *entry;
  GtkWidget *add_btn;
  GtkWidget *hbox;
  GtkWidget *list_box;
  int        max_height = 250;

  g_hash_table_remove_all (self->local_noncore_names);
  custom_nav_load_names (self);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_set_margin_start (box, 8);
  gtk_widget_set_margin_end (box, 8);
  gtk_widget_set_margin_top (box, 8);
  gtk_widget_set_margin_bottom (box, 8);
  gtk_widget_set_size_request (box, 280, -1);

  /* Back to the root menu */
  back_btn = gtk_button_new ();
  gtk_widget_add_css_class (back_btn, "menu-item");
  back_label = gtk_label_new (_ ("← Back"));
  gtk_widget_set_halign (back_label, GTK_ALIGN_START);
  gtk_button_set_child (GTK_BUTTON (back_btn), back_label);
  g_signal_connect (back_btn, "clicked",
                    G_CALLBACK (on_nav_back_clicked), self);
  gtk_box_append (GTK_BOX (box), back_btn);

  /* Entry + Add button row */
  hbox  = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
  entry = gtk_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (entry), "New label name");
  gtk_widget_set_hexpand (entry, TRUE);
  g_signal_connect (entry, "activate",
                    G_CALLBACK (on_custom_nav_add_clicked), self);
  gtk_box_append (GTK_BOX (hbox), entry);

  add_btn = gtk_button_new_with_label ("Add");
  g_object_set_data (G_OBJECT (add_btn), "noncore-entry", entry);
  g_signal_connect (add_btn, "clicked",
                    G_CALLBACK (on_custom_nav_add_btn_clicked), self);
  gtk_box_append (GTK_BOX (hbox), add_btn);

  gtk_box_append (GTK_BOX (box), hbox);

  /* Compute max_height from monitor geometry */
  {
    GdkDisplay  *display;
    GdkMonitor  *monitor;
    GdkSurface  *surface;
    GdkRectangle geometry;

    display = gtk_widget_get_display (GTK_WIDGET (self));
    surface = gtk_native_get_surface (GTK_NATIVE (gtk_widget_get_root (GTK_WIDGET (self))));
    monitor = gdk_display_get_monitor_at_surface (display, surface);
    if (monitor != NULL)
      {
        gdk_monitor_get_geometry (monitor, &geometry);
        max_height = geometry.height * 8 / 10; // 80%
        if (max_height > 600)
          max_height = 600;
      }
  }

  /* Build list of existing names with delete buttons */
  list_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  if (g_hash_table_size (self->local_noncore_names) == 0)
    {
      GtkWidget *empty = gtk_label_new ("No custom labels");
      gtk_widget_set_margin_start (empty, 6);
      gtk_widget_set_margin_end (empty, 6);
      gtk_widget_set_margin_top (empty, 6);
      gtk_widget_set_margin_bottom (empty, 6);
      gtk_box_append (GTK_BOX (list_box), empty);
    }
  else
    {
      GHashTableIter iter;
      gpointer       k;
      GPtrArray     *sorted;

      sorted = g_ptr_array_new ();
      g_hash_table_iter_init (&iter, self->local_noncore_names);
      while (g_hash_table_iter_next (&iter, &k, NULL))
        g_ptr_array_add (sorted, k);
      g_ptr_array_sort (sorted, compare_names);

      for (guint i = 0; i < sorted->len; i++)
        {
          const char *name = (const char *) g_ptr_array_index (sorted, i);
          GtkWidget  *row;
          GtkWidget  *label;
          GtkWidget  *del_btn;

          row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
          gtk_widget_set_margin_start (row, 4);
          gtk_widget_set_margin_end (row, 20);
          gtk_widget_set_margin_top (row, 1);
          gtk_widget_set_margin_bottom (row, 1);

          label = gtk_label_new (name);
          gtk_widget_set_hexpand (label, TRUE);
          gtk_widget_set_halign (label, GTK_ALIGN_START);
          gtk_box_append (GTK_BOX (row), label);

          del_btn = gtk_button_new_from_icon_name ("user-trash-symbolic");
          gtk_widget_add_css_class (del_btn, "flat");
          gtk_widget_add_css_class (del_btn, "circular");
          g_object_set_data (G_OBJECT (del_btn), "noncore-name",
                             (gpointer) name);
          g_signal_connect (del_btn, "clicked",
                            G_CALLBACK (on_custom_nav_delete_clicked), self);
          gtk_box_append (GTK_BOX (row), del_btn);

          gtk_box_append (GTK_BOX (list_box), row);
        }
      g_ptr_array_unref (sorted);
    }

  /* Always use GtkScrolledWindow with propagate-natural-height so
   * the scroll window reports its child's natural height to the parent,
   * capped at max_height. Small lists shrink; large lists get a scrollbar. */
  {
    GtkWidget *scroll = gtk_scrolled_window_new ();
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_overlay_scrolling (GTK_SCROLLED_WINDOW (scroll), FALSE);
    gtk_scrolled_window_set_propagate_natural_height (GTK_SCROLLED_WINDOW (scroll), TRUE);
    gtk_scrolled_window_set_max_content_height (GTK_SCROLLED_WINDOW (scroll), max_height);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), list_box);
    gtk_box_append (GTK_BOX (box), scroll);

    g_debug ("[DIAG] rebuild_custom_nav_popup: n_items=%u, max_height=%d",
             g_hash_table_size (self->local_noncore_names), max_height);
  }

  self->rebuilding_popup = TRUE;
  gtk_window_set_child (self->custom_nav_popup, box);
  self->rebuilding_popup = FALSE;
}

static void
rebuild_custom_nav_popup (BzWindow *self)
{
  if (self->current_nav_view == NAV_VIEW_LABELS)
    rebuild_custom_nav_labels_view (self);
  else
    rebuild_custom_nav_root_view (self);
}

static void
on_view_stack_visible_child_changed (BzWindow   *self,
                                     GParamSpec *pspec,
                                     gpointer    user_data)
{
  const char *visible = adw_view_stack_get_visible_child_name (
      self->main_view_stack);

  gtk_widget_set_visible (GTK_WIDGET (self->custom_nav_button),
                          g_strcmp0 (visible, "custom") == 0);
}

static void
on_custom_nav_popup_focus_leave (GtkEventControllerFocus *ctrl,
                                 gpointer                 user_data)
{
  BzWindow *self = BZ_WINDOW (user_data);

  g_debug ("[DIAG] on_custom_nav_popup_focus_leave: entering, popup_visible=%d, rebuilding=%d",
           self->popup_visible, self->rebuilding_popup);

  if (self->popup_visible && !self->rebuilding_popup)
    {
      self->popup_visible = FALSE;
      gtk_widget_set_visible (GTK_WIDGET (self->custom_nav_popup), FALSE);
    }
}

static gboolean
on_custom_nav_popup_key_pressed (GtkEventControllerKey *ctrl,
                                 guint                  keyval,
                                 guint                  keycode,
                                 GdkModifierType        state,
                                 gpointer               user_data)
{
  BzWindow *self = BZ_WINDOW (user_data);

  if (keyval == GDK_KEY_Escape)
    {
      g_debug ("[DIAG] on_custom_nav_popup_key_pressed: Escape pressed, hiding popup");
      if (self->popup_visible)
        {
          self->popup_visible = FALSE;
          gtk_widget_set_visible (GTK_WIDGET (self->custom_nav_popup), FALSE);
        }
      return TRUE;
    }
  return FALSE;
}

static void
on_custom_nav_button_clicked (GtkButton *button,
                              gpointer   user_data)
{
  BzWindow *self = BZ_WINDOW (user_data);

  g_debug ("[DIAG] on_custom_nav_button_clicked: popup_visible=%d",
           self->popup_visible);

  if (self->popup_visible)
    {
      self->popup_visible = FALSE;
      gtk_widget_set_visible (GTK_WIDGET (self->custom_nav_popup), FALSE);
    }
  else
    {
      self->current_nav_view = NAV_VIEW_ROOT;
      rebuild_custom_nav_popup (self);
      self->popup_visible = TRUE;
      gtk_widget_set_visible (GTK_WIDGET (self->custom_nav_popup), TRUE);
      gtk_window_present (self->custom_nav_popup);
    }
}

/* ------------------------------------------------------------------ */
/*  Empty DB recovery dialog                                           */
/* ------------------------------------------------------------------ */

static void
on_restore_latest_clicked (AdwAlertDialog *alert,
                           const char     *response,
                           gpointer         user_data)
{
  BzWindow    *self = BZ_WINDOW (user_data);
  char        *latest_path;
  g_autoptr (GError) err = NULL;

  if (g_strcmp0 (response, "restore-latest") != 0)
    return;

  latest_path = bz_label_store_get_latest_backup_path (self->label_store);
  if (latest_path == NULL)
    return;

  g_debug ("[DIAG] on_restore_latest_clicked: restoring from %s", latest_path);

  bz_label_store_close (self->label_store);
  self->label_store = NULL;

  if (g_file_set_contents (self->db_path, NULL, 0, &err))
    {
      g_debug ("[DIAG] on_restore_latest_clicked: cleared DB file");
    }
  else
    {
      g_warning ("Failed to clear DB file: %s", err->message);
      g_clear_error (&err);
    }

  {
    char *data_dir = g_path_get_dirname (self->db_path);
    self->label_store = bz_label_store_open (self->db_path, data_dir, NULL);
    g_free (data_dir);
  }

  if (self->label_store != NULL)
    {
      g_autoptr (GError) copy_err = NULL;
      gchar             *data     = NULL;
      gsize              len      = 0;

      if (g_file_get_contents (latest_path, &data, &len, &copy_err))
        {
          g_file_set_contents (self->db_path, data, len, NULL);
          g_free (data);

          bz_label_store_close (self->label_store);
          self->label_store = NULL;

          {
            char *data_dir = g_path_get_dirname (self->db_path);
            self->label_store = bz_label_store_open (self->db_path, data_dir, NULL);
            g_free (data_dir);
          }
        }
      else
        {
          g_warning ("Failed to read backup: %s", copy_err->message);
        }
    }

  self->db_empty = FALSE;
  g_free (latest_path);
}

static void
on_backup_file_chosen (GtkFileDialog *dialog,
                       GAsyncResult  *result,
                       gpointer       user_data)
{
  BzWindow           *self = BZ_WINDOW (user_data);
  g_autoptr (GFile)   file = NULL;
  g_autoptr (GError)  err  = NULL;

  file = gtk_file_dialog_open_finish (dialog, result, &err);
  if (file == NULL)
    {
      if (err != NULL && !g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("File chooser failed: %s", err->message);
      return;
    }

  {
    char *chosen_path = g_file_get_path (file);
    g_debug ("[DIAG] on_backup_file_chosen: restoring from %s", chosen_path);

    bz_label_store_close (self->label_store);
    self->label_store = NULL;

    {
      gchar *data = NULL;
      gsize  len  = 0;

      if (g_file_get_contents (chosen_path, &data, &len, NULL))
        {
          g_file_set_contents (self->db_path, data, len, NULL);
          g_free (data);

          {
            char *data_dir = g_path_get_dirname (self->db_path);
            self->label_store = bz_label_store_open (self->db_path, data_dir, NULL);
            g_free (data_dir);
          }
        }
    }

    self->db_empty = FALSE;
    g_free (chosen_path);
  }
}

static void
on_select_backup_clicked (AdwAlertDialog *alert,
                          const char     *response,
                          gpointer         user_data)
{
  BzWindow             *self = BZ_WINDOW (user_data);
  GtkFileDialog        *file_dialog;
  GtkFileFilter        *filter;
  GListStore           *filters;
  char                 *backup_dir;

  if (g_strcmp0 (response, "select-backup") != 0)
    return;

  file_dialog = gtk_file_dialog_new ();
  gtk_file_dialog_set_title (file_dialog, _ ("Select Backup File"));

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _ ("SQLite Database Files"));
  gtk_file_filter_add_pattern (filter, "*.db");

  filters = g_list_store_new (GTK_TYPE_FILE_FILTER);
  g_list_store_append (filters, filter);
  gtk_file_dialog_set_filters (file_dialog, G_LIST_MODEL (filters));

  backup_dir = g_build_filename (g_get_user_data_dir (),
                                 "io.github.kolunmi.Bazaar",
                                 "backups",
                                 NULL);
  {
    g_autoptr (GFile) initial = g_file_new_for_path (backup_dir);
    gtk_file_dialog_set_initial_folder (file_dialog, initial);
  }

  gtk_file_dialog_open (file_dialog, GTK_WINDOW (self), NULL,
                        (GAsyncReadyCallback) on_backup_file_chosen, self);
  g_free (backup_dir);
}

static void
on_recovery_dialog_response (AdwAlertDialog *alert,
                             const char     *response,
                             gpointer         user_data)
{
  g_debug ("[DIAG] on_recovery_dialog_response: response=%s", response);

  if (g_strcmp0 (response, "restore-latest") == 0)
    on_restore_latest_clicked (alert, response, user_data);
  else if (g_strcmp0 (response, "select-backup") == 0)
    on_select_backup_clicked (alert, response, user_data);
}

static void
on_window_mapped_check_db (GtkWindow *window,
                           gpointer   user_data)
{
  BzWindow        *self = BZ_WINDOW (window);
  AdwAlertDialog  *alert;
  char            *backup_path;
  guint            record_count;

  g_signal_handlers_disconnect_by_func (window, on_window_mapped_check_db, user_data);

  if (!self->db_empty || self->label_store == NULL)
    return;

  if (!bz_label_store_has_backups (self->label_store))
    return;

  backup_path = bz_label_store_get_latest_backup_path (self->label_store);
  if (backup_path == NULL)
    return;

  /* Count records in the backup */
  {
    char        *data_dir = g_path_get_dirname (self->db_path);
    BzLabelStore *tmp     = bz_label_store_open (backup_path, data_dir, NULL);
    g_free (data_dir);

    if (tmp != NULL)
      {
        record_count = bz_label_store_count_records (tmp);
        bz_label_store_close (tmp);
      }
    else
      {
        record_count = 0;
      }
  }

  {
    char *body;
    char *basename;

    basename = g_path_get_basename (backup_path);
    body = g_strdup_printf (
        "Latest backup has %u records.\nFile: %s",
        record_count, basename);
    g_free (basename);

    alert = g_object_ref_sink (ADW_ALERT_DIALOG (adw_alert_dialog_new (
        _ ("Database Empty"), body)));
    g_free (body);
  }

  adw_alert_dialog_add_responses (alert,
                                  "cancel", _ ("_Cancel"),
                                  "select-backup", _ ("_Select Backup"),
                                  "restore-latest", _ ("_Restore Latest"),
                                  NULL);
  adw_alert_dialog_set_response_appearance (alert, "restore-latest", ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_default_response (alert, "restore-latest");
  adw_alert_dialog_set_close_response (alert, "cancel");

  g_signal_connect (alert, "response",
                    G_CALLBACK (on_recovery_dialog_response), self);

  adw_dialog_present (ADW_DIALOG (alert), GTK_WIDGET (self));
  g_free (backup_path);
}

static void
bz_window_init (BzWindow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

#ifdef DEVELOPMENT_BUILD
  gtk_widget_add_css_class (GTK_WIDGET (self), "devel");
#endif

  adw_view_stack_set_visible_child_name (self->main_view_stack, "flathub");

  self->key_controller = gtk_event_controller_key_new ();
  g_signal_connect_swapped (self->key_controller,
                            "key-pressed",
                            G_CALLBACK (key_pressed),
                            self);
  gtk_widget_add_controller (GTK_WIDGET (self), self->key_controller);

  /* Custom nav (non-core label management) */
  self->local_noncore_names = g_hash_table_new_full (
      g_str_hash, g_str_equal, g_free, NULL);
  {
    char *data_dir = g_build_filename (g_get_user_data_dir (),
                                       "io.github.kolunmi.Bazaar",
                                       NULL);
    self->db_path = g_build_filename (data_dir, "custom-labels.db", NULL);

    self->label_store = bz_label_store_open (self->db_path, data_dir, NULL);
    g_free (data_dir);

    if (self->label_store != NULL)
      {
        self->db_empty = bz_label_store_is_empty (self->label_store);
        g_debug ("[DIAG] bz_window_init: db_empty=%d, has_backups=%d",
                 self->db_empty,
                 bz_label_store_has_backups (self->label_store));
      }
  }

  /* Custom nav popup (GtkWindow replacing GtkPopover for proper sizing) */
  self->custom_nav_popup = GTK_WINDOW (gtk_window_new ());
  gtk_window_set_transient_for (self->custom_nav_popup, GTK_WINDOW (self));
  gtk_window_set_decorated (self->custom_nav_popup, FALSE);
  gtk_window_set_resizable (self->custom_nav_popup, FALSE);
  gtk_window_set_default_size (self->custom_nav_popup, 280, -1);
  self->popup_visible = FALSE;

  /* Close popup when focus leaves */
  {
    GtkEventController *focus_ctrl = gtk_event_controller_focus_new ();
    g_signal_connect (focus_ctrl, "leave",
                      G_CALLBACK (on_custom_nav_popup_focus_leave), self);
    gtk_widget_add_controller (GTK_WIDGET (self->custom_nav_popup), focus_ctrl);
  }

  /* Close popup on Escape key */
  {
    GtkEventController *key_ctrl = gtk_event_controller_key_new ();
    g_signal_connect (key_ctrl, "key-pressed",
                      G_CALLBACK (on_custom_nav_popup_key_pressed), self);
    gtk_widget_add_controller (GTK_WIDGET (self->custom_nav_popup), key_ctrl);
  }

  /* Toggle popup on button click */
  g_signal_connect (self->custom_nav_button, "clicked",
                    G_CALLBACK (on_custom_nav_button_clicked), self);

  /* Show/hide custom nav button based on visible tab */
  g_signal_connect_object (self->main_view_stack,
                           "notify::visible-child",
                           G_CALLBACK (on_view_stack_visible_child_changed),
                           self, G_CONNECT_SWAPPED);

  /* Schedule empty DB check after window is shown */
  if (self->db_empty && self->label_store != NULL &&
      bz_label_store_has_backups (self->label_store))
    {
      g_signal_connect (self, "map",
                        G_CALLBACK (on_window_mapped_check_db), NULL);
    }
}

static void
app_busy_changed (BzWindow    *self,
                  GParamSpec  *pspec,
                  BzStateInfo *info)
{
  bz_search_page_refresh (self->search_page);
  set_page (self);
}

static void
has_inputs_changed (BzWindow          *self,
                    GParamSpec        *pspec,
                    BzContentProvider *provider)
{
  if (!bz_content_provider_get_has_inputs (provider))
    adw_view_stack_set_visible_child_name (self->main_view_stack, "flathub");
}

static DexFuture *
transact_fiber (TransactData *data)
{
  g_autoptr (GError) local_error        = NULL;
  g_autoptr (BzEntry) selected_entry    = NULL;
  g_autoptr (DexFuture) transact_future = NULL;
  g_autofree char *id_dup               = NULL;
  BzMainConfig    *config               = NULL;
  GListModel      *hooks                = NULL;
  gboolean         delete_user_data     = FALSE;
  GdkDisplay      *display              = NULL;
  GdkSeat         *seat                 = NULL;
  GdkDevice       *keyboard             = NULL;
  GdkModifierType  modifiers            = GDK_NO_MODIFIER_MASK;

  // Get ID early before any async operations
  if (data->group != NULL)
    id_dup = g_strdup (bz_entry_group_get_id (data->group));
  else
    id_dup = g_strdup (bz_entry_get_id (data->entry));

  /* Prevent Bazaar from being removed by itself */
  if (data->remove)
    {
      const char *bazaar_id = NULL;

      bazaar_id = g_application_get_application_id (g_application_get_default ());
      if (g_strcmp0 (id_dup, bazaar_id) == 0)
        {
          GtkWidget *window = NULL;
          window            = GTK_WIDGET (gtk_application_get_active_window (GTK_APPLICATION (g_application_get_default ())));
          bz_show_error_for_widget (window, _ ("You can't remove Bazaar from Bazaar!"), _ ("You can't remove Bazaar from Bazaar!"));
          return dex_future_new_false ();
        }
    }

  config = bz_state_info_get_main_config (bz_state_info_get_default ());
  if (config != NULL)
    hooks = bz_main_config_get_hooks (config);

#define RUN_HOOK(_signal)                                               \
  G_STMT_START                                                          \
  {                                                                     \
    if (hooks != NULL &&                                                \
        !dex_await (                                                    \
            bz_run_hook_emission (                                      \
                hooks, (_signal),                                       \
                data->remove                                            \
                    ? BZ_HOOK_TRANSACTION_TYPE_REMOVAL                  \
                    : BZ_HOOK_TRANSACTION_TYPE_INSTALL,                 \
                id_dup, NULL),                                          \
            &local_error))                                              \
      return dex_future_new_for_error (g_steal_pointer (&local_error)); \
  }                                                                     \
  G_STMT_END

  RUN_HOOK (BZ_HOOK_SIGNAL_BEFORE_TRANSACTION);

  display  = gdk_display_get_default ();
  seat     = gdk_display_get_default_seat (display);
  keyboard = gdk_seat_get_keyboard (seat);
  if (keyboard != NULL)
    modifiers = gdk_device_get_modifier_state (keyboard);

  if (modifiers & GDK_SHIFT_MASK)
    /* Holding shift while invoking a transaction skips the dialog and assumes
       the first valid entry */
    {
      if (data->group != NULL)
        {
          g_autoptr (GListModel) store = NULL;
          guint n_items                = 0;

          store = dex_await_object (
              bz_entry_group_dup_all_into_store (data->group),
              &local_error);
          if (store == NULL)
            return dex_future_new_for_error (g_steal_pointer (&local_error));

          n_items = g_list_model_get_n_items (store);
          for (guint i = 0; i < n_items; i++)
            {
              g_autoptr (BzEntry) entry = NULL;
              gboolean installed        = FALSE;

              entry     = g_list_model_get_item (store, i);
              installed = bz_entry_is_installed (entry);
              if ((data->remove && installed) ||
                  (!data->remove && !installed))
                {
                  selected_entry = g_steal_pointer (&entry);
                  break;
                }
            }
          if (selected_entry == NULL)
            return dex_future_new_false ();
        }
      else
        selected_entry = g_object_ref (data->entry);
    }
  else
    {
      g_autoptr (BzTransactionDialogResult) dialog_result = NULL;

      // Show the dialog
      dialog_result = dex_await_object (
          bz_transaction_dialog_show (
              GTK_WIDGET (gtk_application_get_active_window (
                  GTK_APPLICATION (g_application_get_default ()))),
              data->entry,
              data->group,
              data->remove,
              data->auto_confirm),
          &local_error);

      if (dialog_result == NULL)
        return dex_future_new_for_error (g_steal_pointer (&local_error));
      if (!bz_transaction_dialog_result_get_confirmed (dialog_result))
        return dex_future_new_false ();

      selected_entry = g_object_ref (
          bz_transaction_dialog_result_get_selected_entry (dialog_result));
      delete_user_data = bz_transaction_dialog_result_get_delete_user_data (
          dialog_result);
    }

  // Perform the transaction
  transact_future = transact (
      selected_entry,
      data->remove,
      data->source);

  if (!dex_await (g_steal_pointer (&transact_future), &local_error))
    return dex_future_new_for_error (g_steal_pointer (&local_error));

  // Handle user data deletion
  if (delete_user_data)
    {
      if (data->group != NULL)
        bz_entry_group_reap_user_data (data->group);
      else
        dex_future_disown (bz_reap_user_data_dex (id_dup));
    }

  RUN_HOOK (BZ_HOOK_SIGNAL_AFTER_TRANSACTION);
#undef RUN_HOOK

  return dex_future_new_true ();
}

void
bz_window_set_custom_label_callbacks (BzWindow                *self,
                                      BzNoncoreNameAddedFunc   added,
                                      BzNoncoreNameRemovedFunc removed,
                                      gpointer                 user_data)
{
  g_return_if_fail (BZ_IS_WINDOW (self));
  self->noncore_name_added_cb   = added;
  self->noncore_name_removed_cb = removed;
  self->noncore_name_cb_data    = user_data;
}

BzWindow *
bz_window_new (BzStateInfo *state)
{
  BzWindow     *window = NULL;
  BzMainConfig *config = NULL;

  g_return_val_if_fail (BZ_IS_STATE_INFO (state), NULL);

  window        = g_object_new (BZ_TYPE_WINDOW, NULL);
  window->state = g_object_ref (state);

  config = bz_state_info_get_main_config (state);
  if (config != NULL && bz_main_config_get_start_on_curated (config))
    adw_view_stack_set_visible_child_name (window->main_view_stack, "browse");

  g_signal_connect_object (state,
                           "notify::busy",
                           G_CALLBACK (app_busy_changed),
                           window, G_CONNECT_SWAPPED);

  if (bz_state_info_get_curated_provider (state) != NULL)
    g_signal_connect_object (bz_state_info_get_curated_provider (state),
                             "notify::has-inputs",
                             G_CALLBACK (has_inputs_changed),
                             window, G_CONNECT_SWAPPED);

  g_object_notify_by_pspec (G_OBJECT (window), props[PROP_STATE]);

  set_page (window);
  return window;
}

void
bz_window_search (BzWindow   *self,
                  const char *text)
{
  g_return_if_fail (BZ_IS_WINDOW (self));
  search (self, text);
}

void
bz_window_show_entry (BzWindow *self,
                      BzEntry  *entry)
{
  g_autoptr (BzEntryGroup) group = NULL;

  g_return_if_fail (BZ_IS_WINDOW (self));
  g_return_if_fail (BZ_IS_ENTRY (entry));

  group = bz_entry_group_new_for_single_entry (entry);
  bz_window_show_group (self, group);
}

void
bz_window_show_group (BzWindow     *self,
                      BzEntryGroup *group)
{
  GListModel *stack    = NULL;
  gboolean    in_stack = FALSE;

  g_return_if_fail (BZ_IS_WINDOW (self));
  g_return_if_fail (BZ_IS_ENTRY_GROUP (group));

  bz_full_view_set_entry_group (self->full_view, group);
  emit_hook_disown (self, BZ_HOOK_SIGNAL_VIEW_APP, group);
  stack = adw_navigation_view_get_navigation_stack (self->navigation_view);

  for (guint i = 0; i < g_list_model_get_n_items (stack); i++)
    {
      g_autoptr (AdwNavigationPage) page = NULL;
      page                               = g_list_model_get_item (stack, i);

      if (g_strcmp0 (adw_navigation_page_get_tag (page), "view") == 0)
        {
          in_stack = TRUE;
          adw_navigation_view_pop_to_page (self->navigation_view, page);
          break;
        }
    }
  if (!in_stack)
    adw_navigation_view_push_by_tag (self->navigation_view, "view");
}

void
bz_window_add_toast (BzWindow *self,
                     AdwToast *toast)
{
  g_return_if_fail (BZ_IS_WINDOW (self));
  g_return_if_fail (ADW_IS_TOAST (toast));

  adw_toast_overlay_add_toast (self->toasts, toast);
}

void
bz_window_push_page (BzWindow *self, AdwNavigationPage *page)
{
  g_return_if_fail (BZ_IS_WINDOW (self));
  g_return_if_fail (ADW_IS_NAVIGATION_PAGE (page));

  adw_navigation_view_push (self->navigation_view, page);
}

void
bz_window_open_screenshot_page (BzWindow         *self,
                                BzScreenshotPage *page)
{
  g_return_if_fail (BZ_IS_WINDOW (self));
  g_return_if_fail (BZ_IS_SCREENSHOT_PAGE (page));

  if (self->screenshot_page != NULL)
    return;

  self->screenshot_page = page;
  g_signal_connect_swapped (page, "destroy",
                            G_CALLBACK (g_nullify_pointer),
                            &self->screenshot_page);
  gtk_overlay_add_overlay (self->window_overlay, GTK_WIDGET (page));
}

void
bz_window_bulk_install (BzWindow   *self,
                        GListModel *groups)
{
  g_autoptr (BulkInstallData) data = NULL;

  g_return_if_fail (BZ_IS_WINDOW (self));
  g_return_if_fail (G_IS_LIST_MODEL (groups));

  data         = bulk_install_data_new ();
  data->self   = bz_track_weak (self);
  data->groups = g_object_ref (groups);

  dex_future_disown (dex_scheduler_spawn (
      dex_scheduler_get_default (),
      bz_get_dex_stack_size (),
      (DexFiberFunc) bulk_install_fiber,
      bulk_install_data_ref (data),
      bulk_install_data_unref));
}

BzStateInfo *
bz_window_get_state_info (BzWindow *self)
{
  g_return_val_if_fail (BZ_IS_WINDOW (self), NULL);
  return self->state;
}

static DexFuture *
transact (BzEntry   *entry,
          gboolean   remove,
          GtkWidget *source)
{
  g_autoptr (BzTransaction) transaction = NULL;

  if (remove)
    transaction = bz_transaction_new_full (
        NULL, 0,
        NULL, 0,
        &entry, 1);
  else
    transaction = bz_transaction_new_full (
        &entry, 1,
        NULL, 0,
        NULL, 0);

  return bz_transaction_manager_add (
      bz_state_info_get_transaction_manager (bz_state_info_get_default ()),
      transaction);
}

static void
try_transact (BzWindow     *self,
              BzEntry      *entry,
              BzEntryGroup *group,
              gboolean      remove,
              gboolean      auto_confirm,
              GtkWidget    *source)
{
  g_autoptr (TransactData) data = NULL;

  g_return_if_fail (entry != NULL || group != NULL);
  if (bz_state_info_get_busy (self->state))
    {
      adw_toast_overlay_add_toast (
          self->toasts,
          adw_toast_new_format (_ ("Can't do that right now!")));
      return;
    }

  data               = transact_data_new ();
  data->self         = bz_track_weak (self);
  data->entry        = bz_object_maybe_ref (entry);
  data->group        = bz_object_maybe_ref (group);
  data->remove       = remove;
  data->auto_confirm = auto_confirm;
  data->source       = bz_object_maybe_ref (source);

  dex_future_disown (dex_scheduler_spawn (
      dex_scheduler_get_default (),
      bz_get_dex_stack_size (),
      (DexFiberFunc) transact_fiber,
      transact_data_ref (data), transact_data_unref));
}

static void
bulk_install (BzWindow *self,
              BzEntry **installs,
              guint     n_installs)
{
  g_autoptr (BzTransaction) transaction = NULL;

  g_return_if_fail (BZ_IS_WINDOW (self));
  g_return_if_fail (installs != NULL);
  g_return_if_fail (n_installs > 0);

  if (bz_state_info_get_busy (self->state))
    {
      adw_toast_overlay_add_toast (
          self->toasts,
          adw_toast_new_format (_ ("Can't do that right now!")));
      return;
    }

  transaction = bz_transaction_new_full (
      installs, n_installs,
      NULL, 0,
      NULL, 0);

  dex_future_disown (bz_transaction_manager_add (
      bz_state_info_get_transaction_manager (self->state),
      transaction));
}

static DexFuture *
bulk_install_fiber (BulkInstallData *data)
{
  g_autoptr (BzWindow) self                    = NULL;
  g_autoptr (GError) local_error               = NULL;
  g_autoptr (BzBulkInstallDialogResult) result = NULL;
  GListModel          *entries                 = NULL;
  guint                n_installs              = 0;
  g_autofree BzEntry **installs_buf            = NULL;

  bz_weak_get_or_return_reject (self, data->self);

  result = dex_await_object (
      bz_bulk_install_dialog_show (GTK_WIDGET (self), data->groups),
      &local_error);

  if (result == NULL)
    return dex_future_new_for_error (g_steal_pointer (&local_error));

  if (!bz_bulk_install_dialog_result_get_confirmed (result))
    return dex_future_new_false ();

  entries    = bz_bulk_install_dialog_result_get_entries (result);
  n_installs = g_list_model_get_n_items (entries);
  if (n_installs == 0)
    return dex_future_new_false ();

  installs_buf = g_malloc_n (n_installs, sizeof (*installs_buf));
  for (guint i = 0; i < n_installs; i++)
    installs_buf[i] = g_list_model_get_item (entries, i);

  bulk_install (self, installs_buf, n_installs);
  for (guint i = 0; i < n_installs; i++)
    g_object_unref (installs_buf[i]);

  return dex_future_new_true ();
}

static void
search (BzWindow   *self,
        const char *initial)
{
  if (initial != NULL && *initial != '\0')
    bz_search_page_set_text (self->search_page, initial);

  adw_view_stack_set_visible_child_name (self->main_view_stack, "search");
  adw_navigation_view_pop_to_tag (self->navigation_view, "main");
  gtk_widget_grab_focus (GTK_WIDGET (self->search_page));
}

static void
set_page (BzWindow *self)
{
  const char *selected_navigation_page_name = NULL;

  if (self->state == NULL)
    return;

  if (bz_state_info_get_busy (self->state))
    {
      gtk_stack_set_visible_child_name (self->main_stack, "loading");
      adw_navigation_view_pop_to_tag (self->navigation_view, "main");
    }
  else
    gtk_stack_set_visible_child_name (self->main_stack, "main");

  selected_navigation_page_name = adw_navigation_view_get_visible_page_tag (self->navigation_view);

  if (g_strcmp0 (selected_navigation_page_name, "view") != 0)
    bz_full_view_set_entry_group (self->full_view, NULL);
}

static void
emit_hook_disown (BzWindow     *self,
                  BzHookSignal  signal,
                  BzEntryGroup *group)
{
  BzMainConfig *config = NULL;
  GListModel   *hooks  = NULL;

  if (self->state == NULL)
    return;

  config = bz_state_info_get_main_config (self->state);
  if (config == NULL)
    return;

  hooks = bz_main_config_get_hooks (config);
  if (hooks == NULL)
    return;

  dex_future_disown (bz_run_hook_emission (
      hooks, signal, 0, NULL, group));
}
