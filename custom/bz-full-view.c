/* bz-full-view.c
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

#define G_LOG_DOMAIN "BAZAAR::FULL-VIEW-WIDGET"

#include <glib/gi18n.h>

#include "bz-addon-tile.h"
#include "bz-addons-dialog.h"
#include "bz-age-rating-dialog.h"
#include "bz-app-size-dialog.h"
#include "bz-app-tile.h"
#include "bz-apps-page.h"
#include "bz-appstream-description-render.h"
#include "bz-context-tile-callbacks.h"
#include "bz-context-tile.h"
#include "bz-developer-badge.h"
#include "bz-dynamic-list-view.h"
#include "bz-entry-inspector.h"
#include "bz-error.h"
#include "bz-fading-clamp.h"
#include "bz-favorite-button.h"
#include "bz-flatpak-entry.h"
#include "bz-full-view.h"
#include "bz-hardware-support-dialog.h"
#include "bz-install-controls.h"
#include "bz-label-store.h"
#include "bz-license-dialog.h"
#include "bz-metainfo-preview.h"
#include "bz-releases-list.h"
#include "bz-safety-calculator.h"
#include "bz-safety-dialog.h"
#include "bz-screenshot-page.h"
#include "bz-screenshots-carousel.h"
#include "bz-section-view.h"
#include "bz-share-list.h"
#include "bz-spdx.h"
#include "bz-state-info.h"
#include "bz-stats-dialog.h"
#include "bz-template-callbacks.h"
#include "bz-util.h"
#include "bz-window.h"

struct _BzFullView
{
  AdwBin parent_instance;

  BzStateInfo          *state;
  BzTransactionManager *transactions;
  BzEntryGroup         *group;
  DexFuture            *ui_future;
  BzResult             *ui_entry;
  BzResult             *runtime;
  BzResult             *group_model;
  gboolean              show_sidebar;

  /* Template widgets */
  GtkScrolledWindow *main_scroll;
  AdwViewStack      *stack;
  GtkWidget         *shadow_overlay;
  GtkToggleButton   *description_toggle;
  GtkMenuButton     *core_label_button;
  GtkMenuButton     *noncore_label_button;

/* Core labels (shared with custom companion app) */
  GHashTable   *core_labels;
  GHashTable   *core_label_names;     /* set of core label names (New, Install, 4-Stars, 3-Stars, Forget it) */
  BzLabelStore *label_store;
  GtkPopover   *core_label_popover;

  /* Custom labels (user-created, multi-select per app) */
  GHashTable   *custom_labels;        /* app_id -> GPtrArray* of label strings */
  GHashTable   *custom_label_names;   /* set of available custom label names */
  GtkPopover   *custom_label_popover;
};

G_DEFINE_FINAL_TYPE (BzFullView, bz_full_view, ADW_TYPE_BIN)

enum
{
  PROP_0,

  PROP_STATE,
  PROP_ENTRY_GROUP,
  PROP_UI_ENTRY,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

enum
{
  SIGNAL_UPDATE,

  LAST_SIGNAL,
};
static guint signals[LAST_SIGNAL];

static void
bz_label_store_destroy (gpointer data)
{
  bz_label_store_close (data);
}

static void
bz_full_view_dispose (GObject *object)
{
  BzFullView *self = BZ_FULL_VIEW (object);

  dex_clear (&self->ui_future);
  g_clear_object (&self->state);
  g_clear_object (&self->transactions);
  g_clear_object (&self->group);
  g_clear_object (&self->ui_entry);
  g_clear_object (&self->runtime);
  g_clear_object (&self->group_model);

  g_clear_pointer (&self->core_labels, g_hash_table_unref);
  g_clear_pointer (&self->core_label_names, g_hash_table_unref);
  g_clear_pointer (&self->label_store, bz_label_store_destroy);
  /* The popover is parented to core_label_button by
   * gtk_menu_button_set_popover().  Unparent it first to let GTK handle
   * the lifecycle properly — gtk_widget_unparent already drops the ref
   * that set_parent added, so after this the popover may be finalized.
   * Just NULL out our cached pointer and let the parent-class dispose
   * chain finish the template children. */
  if (self->core_label_popover != NULL)
    {
      gtk_widget_unparent (GTK_WIDGET (self->core_label_popover));
      self->core_label_popover = NULL;
    }

  g_clear_pointer (&self->custom_labels, g_hash_table_unref);
  g_clear_pointer (&self->custom_label_names, g_hash_table_unref);
  if (self->custom_label_popover != NULL)
    {
      gtk_widget_unparent (GTK_WIDGET (self->custom_label_popover));
      self->custom_label_popover = NULL;
    }

  G_OBJECT_CLASS (bz_full_view_parent_class)->dispose (object);
}

static void
bz_full_view_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  BzFullView *self = BZ_FULL_VIEW (object);

  switch (prop_id)
    {
    case PROP_STATE:
      g_value_set_object (value, self->state);
      break;
    case PROP_ENTRY_GROUP:
      g_value_set_object (value, bz_full_view_get_entry_group (self));
      break;
    case PROP_UI_ENTRY:
      g_value_set_object (value, self->ui_entry);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_full_view_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  BzFullView *self = BZ_FULL_VIEW (object);

  switch (prop_id)
    {
    case PROP_STATE:
      g_clear_object (&self->state);
      self->state = g_value_dup_object (value);
      break;
    case PROP_ENTRY_GROUP:
      bz_full_view_set_entry_group (self, g_value_get_object (value));
      break;
    case PROP_UI_ENTRY:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static gboolean
is_scrolled_down (gpointer object,
                  double   value)
{
  return value > 100.0;
}

static char *
format_as_link (gpointer    object,
                const char *value)
{
  if (value != NULL)
    return g_strdup_printf ("<a href=\"%s\" title=\"%s\">%s</a>",
                            value, value, value);
  else
    return g_strdup (_ ("No URL"));
}

static gboolean
has_link (gpointer    object,
          const char *license)
{
  if (license == NULL || *license == '\0')
    return FALSE;

  return bz_spdx_is_valid (license);
}

static char *
pick_license_warning (gpointer object,
                      gboolean value)
{
  return value
             ? g_strdup (_ ("This app has a FLOSS license, meaning the source code can be audited for safety."))
             : g_strdup (_ ("This app has a proprietary license, meaning the source code is developed privately and cannot be audited by an independent third party."));
}

static char *
format_other_apps_label (gpointer object, const char *developer)
{
  if (!developer || *developer == '\0')
    return g_strdup (_ ("More Apps"));
  return g_strdup_printf (_ ("More Apps by %s"), developer);
}

static char *
format_more_other_apps_label (gpointer object, const char *developer)
{
  if (!developer || *developer == '\0')
    return g_strdup (_ ("Other Apps by this Developer"));

  return g_strdup_printf (_ ("Other Apps by %s"), developer);
}

static char *
format_leftover_label (gpointer object, const char *name, guint64 size)
{
  g_autofree char *formatted_size = NULL;

  formatted_size = g_format_size (size);
  return g_strdup_printf (_ ("%s is not installed, but it still has <b>%s</b> of data present."), name, formatted_size);
}

static gpointer
filter_own_app_id (BzEntry *entry, GtkStringList *app_ids)
{
  const char *own_id;
  g_autoptr (GtkStringList) filtered = NULL;
  guint n_items                      = 0;

  if (!BZ_IS_ENTRY (entry) || !GTK_IS_STRING_LIST (app_ids))
    return NULL;

  own_id = bz_entry_get_id (entry);
  if (!own_id)
    return NULL;

  filtered = gtk_string_list_new (NULL);
  n_items  = g_list_model_get_n_items (G_LIST_MODEL (app_ids));

  for (guint i = 0; i < n_items; i++)
    {
      const char *id = NULL;

      id = gtk_string_list_get_string (app_ids, i);
      if (g_strcmp0 (id, own_id) != 0)
        gtk_string_list_append (filtered, id);
    }

  if (g_list_model_get_n_items (G_LIST_MODEL (filtered)) > 0)
    return g_steal_pointer (&filtered);
  else
    return NULL;
}

static GListModel *
get_developer_apps_entries (gpointer object, GtkStringList *app_ids, BzEntry *entry)
{
  BzFullView *self                   = BZ_FULL_VIEW (object);
  g_autoptr (GtkStringList) filtered = filter_own_app_id (BZ_ENTRY (entry), app_ids);
  BzApplicationMapFactory *factory;

  if (!filtered)
    return NULL;

  factory = bz_state_info_get_application_factory (self->state);
  if (!factory)
    return NULL;

  return bz_application_map_factory_generate (factory, G_LIST_MODEL (filtered));
}

static int
get_dev_apps_max_children_per_line (gpointer object, GListModel *model)
{
  if (!model)
    return 3;
  return g_list_model_get_n_items (model) > 2 ? 3 : 2;
}

static void
more_apps_button_clicked_cb (BzFullView *self,
                             GtkButton  *button)
{
  g_autoptr (GListModel) model = NULL;
  guint              n_items;
  g_autofree char   *title       = NULL;
  g_autofree char   *subtitle    = NULL;
  AdwNavigationPage *apps_page   = NULL;
  GtkWidget         *nav_view    = NULL;
  g_autoptr (GListModel) app_ids = NULL;
  BzEntry    *entry              = NULL;
  const char *developer          = NULL;

  g_return_if_fail (BZ_IS_FULL_VIEW (self));
  g_return_if_fail (GTK_IS_BUTTON (button));

  entry = bz_result_get_object (self->ui_entry);
  if (entry == NULL)
    return;

  g_object_get (entry, "developer-apps", &app_ids, NULL);

  model = bz_application_map_factory_generate (
      bz_state_info_get_application_factory (self->state),
      app_ids);

  n_items = g_list_model_get_n_items (model);

  developer = bz_entry_get_developer (entry);
  if (developer != NULL && *developer != '\0')
    title = g_strdup_printf (_ ("Other Apps by %s"), developer);
  else
    title = g_strdup (_ ("Other Apps"));

  subtitle = g_strdup_printf (ngettext ("%d Application", "%d Applications", n_items), n_items);

  apps_page = bz_apps_page_new (title, model);
  bz_apps_page_set_subtitle (BZ_APPS_PAGE (apps_page), subtitle);

  nav_view = gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_NAVIGATION_VIEW);
  if (nav_view != NULL)
    adw_navigation_view_push (ADW_NAVIGATION_VIEW (nav_view), apps_page);
}

static void
app_tile_clicked_cb (BzFullView *self,
                     BzAppTile  *tile)
{
  BzEntryGroup *group = bz_app_tile_get_group (tile);
  bz_full_view_set_entry_group (self, group);
}

static void
bind_app_tile_cb (BzFullView        *self,
                  BzAppTile         *tile,
                  BzEntryGroup      *group,
                  BzDynamicListView *view)
{
  g_signal_connect_swapped (tile, "clicked",
                            G_CALLBACK (app_tile_clicked_cb),
                            self);
}

static void
unbind_app_tile_cb (BzFullView        *self,
                    BzAppTile         *tile,
                    BzEntryGroup      *group,
                    BzDynamicListView *view)
{
  g_signal_handlers_disconnect_by_func (tile, G_CALLBACK (app_tile_clicked_cb), self);
}

static void
open_url_cb (BzFullView   *self,
             AdwActionRow *row)
{
  BzEntry    *entry = NULL;
  const char *url   = NULL;

  entry = BZ_ENTRY (bz_result_get_object (self->ui_entry));
  url   = bz_entry_get_url (entry);

  if (url != NULL && *url != '\0')
    g_app_info_launch_default_for_uri (url, NULL, NULL);
  else
    g_warning ("Invalid or empty URL provided for Flathub URL CB");
}

static void
license_cb (BzFullView *self,
            GtkButton  *button)
{
  AdwDialog *dialog   = NULL;
  BzEntry   *ui_entry = NULL;

  if (self->group == NULL)
    return;

  ui_entry = bz_result_get_object (self->ui_entry);
  if (ui_entry == NULL)
    return;

  dialog = bz_license_dialog_new (ui_entry);
  adw_dialog_present (dialog, GTK_WIDGET (self));
}

static void
age_rating_cb (BzFullView *self,
               GtkButton  *button)
{
  BzAgeRatingDialog *dialog   = NULL;
  BzEntry           *ui_entry = NULL;

  if (self->group == NULL)
    return;

  ui_entry = bz_result_get_object (self->ui_entry);
  if (ui_entry == NULL)
    return;

  dialog = bz_age_rating_dialog_new (ui_entry);
  adw_dialog_present (ADW_DIALOG (dialog), GTK_WIDGET (self));
}

static void
dl_stats_cb (BzFullView *self,
             GtkButton  *button)
{
  AdwDialog        *dialog   = NULL;
  AdwBreakpointBin *bin      = NULL;
  BzEntry          *ui_entry = NULL;

  if (self->group == NULL)
    return;

  ui_entry = bz_result_get_object (self->ui_entry);

  bin    = bz_stats_dialog_new (NULL, NULL, 0);
  dialog = adw_dialog_new ();
  adw_dialog_set_content_width (dialog, 1250);
  adw_dialog_set_content_height (dialog, 750);
  adw_dialog_set_child (dialog, GTK_WIDGET (bin));

  g_object_bind_property (ui_entry, "download-stats", bin, "model", G_BINDING_SYNC_CREATE);
  g_object_bind_property (ui_entry, "download-stats-per-country", bin, "country-model", G_BINDING_SYNC_CREATE);
  g_object_bind_property (ui_entry, "total-downloads", bin, "total-downloads", G_BINDING_SYNC_CREATE);

  adw_dialog_present (dialog, GTK_WIDGET (self));
  bz_stats_dialog_animate_open (BZ_STATS_DIALOG (bin));
}

static void
size_cb (BzFullView *self,
         GtkButton  *button)
{
  AdwDialog *size_dialog = NULL;

  if (self->group == NULL)
    return;

  size_dialog = bz_app_size_dialog_new (self->group);
  adw_dialog_present (size_dialog, GTK_WIDGET (self));
}

static void
formfactor_cb (BzFullView *self,
               GtkButton  *button)
{
  AdwDialog *dialog   = NULL;
  BzEntry   *ui_entry = NULL;

  if (self->group == NULL)
    return;

  ui_entry = bz_result_get_object (self->ui_entry);
  dialog   = ADW_DIALOG (bz_hardware_support_dialog_new (ui_entry));

  adw_dialog_present (dialog, GTK_WIDGET (self));
}

static void
safety_cb (BzFullView *self,
           GtkButton  *button)
{
  AdwDialog *dialog   = NULL;
  BzEntry   *ui_entry = NULL;

  if (self->group == NULL)
    return;

  ui_entry = bz_result_get_object (self->ui_entry);
  if (ui_entry == NULL)
    return;

  dialog = bz_safety_dialog_new (ui_entry);
  adw_dialog_present (dialog, GTK_WIDGET (self));
}

static void
update_cb (BzFullView        *self,
           GListModel        *entries,
           BzInstallControls *controls)
{
  g_signal_emit (self, signals[SIGNAL_UPDATE], 0, entries);
}

static DexFuture *
reap_user_data_done (DexFuture *future,
                     GWeakRef  *wr)
{
  g_autoptr (BzFullView) self    = NULL;
  g_autoptr (GError) local_error = NULL;

  dex_future_get_value (future, &local_error);

  self = g_weak_ref_get (wr);
  if (self != NULL && local_error != NULL)
    bz_show_error_for_widget (
        GTK_WIDGET (gtk_widget_get_root (GTK_WIDGET (self))),
        _ ("Failed to Remove User Data"),
        local_error->message);

  return dex_future_new_true ();
}

static void
delete_user_data_cb (BzFullView *self,
                     GtkButton  *button)
{
  g_autoptr (DexFuture) future = NULL;

  g_return_if_fail (BZ_IS_FULL_VIEW (self));

  if (self->group == NULL)
    return;

  future = bz_entry_group_reap_user_data (self->group);
  if (future != NULL)
    dex_future_disown (dex_future_finally (
        dex_ref (future),
        (DexFutureCallback) reap_user_data_done,
        bz_track_weak (self),
        bz_weak_release));
}

static void
support_cb (BzFullView *self,
            GtkButton  *button)
{
  BzEntry *entry = NULL;

  entry = bz_result_get_object (self->ui_entry);
  if (entry != NULL)
    {
      const char *url = NULL;

      url = bz_entry_get_donation_url (entry);
      g_app_info_launch_default_for_uri (url, NULL, NULL);
    }
}

static GListModel *
get_addon_groups (BzFullView   *self,
                  BzEntryGroup *group)
{
  BzApplicationMapFactory *factory = NULL;
  GListModel              *ids     = NULL;
  GListModel              *groups  = NULL;

  if (group == NULL)
    return NULL;

  ids = bz_entry_group_get_addon_group_ids (group);
  if (ids == NULL)
    return NULL;

  factory = bz_state_info_get_application_factory (self->state);
  groups  = bz_application_map_factory_generate (factory, ids);
  if (groups == NULL)
    return NULL;

  return G_LIST_MODEL (gtk_slice_list_model_new (groups, 0, 3));
}

static gboolean
should_show_addon_overflow (gpointer      object,
                            BzEntryGroup *group)
{
  GListModel *ids = NULL;

  if (group == NULL)
    return FALSE;

  ids = bz_entry_group_get_addon_group_ids (group);
  if (ids == NULL)
    return FALSE;

  return g_list_model_get_n_items (ids) > 3;
}

static void
install_addons_cb (BzFullView *self,
                   GtkButton  *button)
{
  if (self->group == NULL)
    return;

  gtk_widget_activate_action (GTK_WIDGET (self), "window.addons-group", "s",
                              bz_entry_group_get_id (self->group));
}

static void
addon_tile_activated_cb (BzAddonTile *tile)
{
  BzFullView   *self   = NULL;
  BzEntryGroup *group  = NULL;
  AdwDialog    *dialog = NULL;

  self  = BZ_FULL_VIEW (gtk_widget_get_ancestor (GTK_WIDGET (tile), BZ_TYPE_FULL_VIEW));
  group = bz_addon_tile_get_group (tile);

  if (group == NULL)
    return;

  dialog = bz_addons_dialog_new_single (group);
  adw_dialog_present (dialog, GTK_WIDGET (self));
}

static int
get_description_max_height (gpointer object,
                            gboolean active)
{
  return active ? 10000 : 170;
}

static char *
get_description_toggle_text (gpointer object,
                             gboolean active)
{
  return g_strdup (active ? _ ("Show Less") : _ ("Show More"));
}

static gboolean
metainfo_banner_visible (gpointer    object,
                         const char *remote_name)
{
  return g_strcmp0 (remote_name, "local-preview") == 0;
}

static void
preview_other_metainfo_cb (BzFullView *self,
                           AdwBanner  *banner)
{
  GtkWidget *window = NULL;

  window = GTK_WIDGET (gtk_widget_get_root (GTK_WIDGET (self)));
  bz_window_push_page (BZ_WINDOW (window),
                       create_entry_group_preview_page (self->group));
}

static void
copy_id_cb (BzFullView *self,
            GtkButton  *button)
{
  const char   *id        = NULL;
  GdkClipboard *clipboard = NULL;

  if (self->group == NULL)
    return;
  id = bz_entry_group_get_id (self->group);

  clipboard = gdk_display_get_clipboard (gdk_display_get_default ());
  gdk_clipboard_set_text (clipboard, id);
}

static void
debug_id_inspect_cb (BzFullView *self,
                     GtkButton  *button)
{
  g_autofree char *unique_id         = NULL;
  g_autoptr (GtkStringObject) string = NULL;
  g_autoptr (BzResult) result        = NULL;

  if (self->group == NULL)
    return;
  unique_id = bz_entry_group_dup_ui_entry_id (self->group);

  result = bz_application_map_factory_convert_one (
      bz_state_info_get_entry_factory (self->state),
      gtk_string_object_new (unique_id));
  if (result != NULL)
    {
      BzEntryInspector *inspector = NULL;

      inspector = bz_entry_inspector_new ();
      bz_entry_inspector_set_result (inspector, result);

      gtk_window_present (GTK_WINDOW (inspector));
    }
}

/* ------------------------------------------------------------------ */
/*  Label persistence (SQLite store shared with custom companion app)  */
/* ------------------------------------------------------------------ */

/* Load the full label state from the store into the in-memory maps. */
static void
load_label_maps (BzFullView *self)
{
  char **app_ids;
  char **names;
  guint  i;

  g_hash_table_remove_all (self->core_labels);
  g_hash_table_remove_all (self->core_label_names);
  g_hash_table_remove_all (self->custom_labels);
  g_hash_table_remove_all (self->custom_label_names);

  app_ids = bz_label_store_get_core_app_ids (self->label_store);
  if (app_ids != NULL)
    {
      for (i = 0; app_ids[i] != NULL; i++)
        {
          g_autofree char *label =
              bz_label_store_get_core_label (self->label_store, app_ids[i]);
          g_hash_table_insert (self->core_labels,
                               g_strdup (app_ids[i]),
                               g_strdup (label));
        }
      g_strfreev (app_ids);
    }

  app_ids = bz_label_store_get_noncore_app_ids (self->label_store);
  if (app_ids != NULL)
    {
      for (i = 0; app_ids[i] != NULL; i++)
        {
          char **labels = bz_label_store_get_noncore_labels (self->label_store,
                                                              app_ids[i]);
          if (labels != NULL && labels[0] != NULL)
            {
              GPtrArray *label_array = g_ptr_array_new_with_free_func (g_free);
              for (guint j = 0; labels[j] != NULL; j++)
                g_ptr_array_add (label_array, g_strdup (labels[j]));
              g_hash_table_insert (self->custom_labels,
                                   g_strdup (app_ids[i]),
                                   label_array);
              for (guint j = 0; labels[j] != NULL; j++)
                g_hash_table_add (self->custom_label_names, g_strdup (labels[j]));
            }
          g_strfreev (labels);
        }
      g_strfreev (app_ids);
    }

  /* Load core label names */
  names = bz_label_store_get_all_core_label_names (self->label_store);
  if (names != NULL)
    {
      for (i = 0; names[i] != NULL; i++)
        g_hash_table_add (self->core_label_names, g_strdup (names[i]));
      g_strfreev (names);
    }

  /* Load custom label names */
  names = bz_label_store_get_all_label_names (self->label_store);
  if (names != NULL)
    {
      for (i = 0; names[i] != NULL; i++)
        g_hash_table_add (self->custom_label_names, g_strdup (names[i]));
      g_strfreev (names);
    }
}

/*  Core label popover                                                  */
/* ------------------------------------------------------------------ */

static void
on_core_label_item_clicked (GtkButton *button,
                            void      *user_data)
{
  BzFullView   *self = BZ_FULL_VIEW (user_data);
  const char   *label;
  BzEntryGroup *group;
  const char   *app_id;

  label = (const char *) g_object_get_data (G_OBJECT (button), "core-label");
  if (label == NULL)
    return;

  group = self->group;
  if (group == NULL)
    return;

  app_id = bz_entry_group_get_id (group);
  g_hash_table_insert (self->core_labels,
                       g_strdup (app_id),
                       g_strdup (label));
  bz_label_store_set_core_label (self->label_store, app_id, label, NULL);
  gtk_popover_popdown (GTK_POPOVER (self->core_label_popover));
}

static void
rebuild_core_label_popover (BzFullView *self)
{
  static const char *labels[] = { "New", "Install", "4-Stars", "3-Stars", "Forget it", NULL };
  GtkWidget         *box;
  const char        *current_app_id;
  const char        *current_label;
  guint              i;

  g_test_message ("DIAG:rebuild | self=%p | group=%p | group_type=%s",
                  (void *) self,
                  (void *) self->group,
                  self->group != NULL ? G_OBJECT_TYPE_NAME (self->group) : "(null)");
  gtk_popover_set_child (self->core_label_popover, NULL);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  if (self->group == NULL)
    {
      g_test_message ("DIAG:rebuild | TAKING NULL PATH");
      GtkWidget *no_app = gtk_label_new ("No app selected");
      gtk_widget_set_margin_start (no_app, 12);
      gtk_widget_set_margin_end (no_app, 12);
      gtk_widget_set_margin_top (no_app, 6);
      gtk_widget_set_margin_bottom (no_app, 6);
      gtk_box_append (GTK_BOX (box), no_app);
      gtk_popover_set_child (self->core_label_popover, box);
      return;
    }

  g_test_message ("DIAG:rebuild | TAKING NON-NULL PATH");
  current_app_id = bz_entry_group_get_id (self->group);
  current_label  = (const char *) g_hash_table_lookup (self->core_labels,
                                                       current_app_id);

  for (i = 0; labels[i] != NULL; i++)
    {
      GtkWidget *button;
      gboolean   is_selected;

      is_selected = (g_strcmp0 (labels[i], current_label) == 0 || (current_label == NULL && i == 0));

      button = gtk_button_new ();
      gtk_widget_set_margin_start (button, 6);
      gtk_widget_set_margin_end (button, 6);
      gtk_widget_set_margin_top (button, 2);
      gtk_widget_set_margin_bottom (button, 2);

      if (is_selected)
        {
          GtkWidget *hbox;
          GtkWidget *icon;

          gtk_widget_add_css_class (button, "suggested-action");

          hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
          icon = gtk_image_new_from_icon_name ("object-select-symbolic");
          gtk_box_append (GTK_BOX (hbox), icon);
          gtk_box_append (GTK_BOX (hbox), gtk_label_new (labels[i]));

          gtk_button_set_child (GTK_BUTTON (button), hbox);
        }
      else
        gtk_button_set_child (GTK_BUTTON (button),
                              gtk_label_new (labels[i]));

      g_object_set_data (G_OBJECT (button), "core-label", (gpointer) labels[i]);
      g_signal_connect (button, "clicked",
                        G_CALLBACK (on_core_label_item_clicked), self);
      gtk_box_append (GTK_BOX (box), button);
    }

  gtk_popover_set_child (self->core_label_popover, box);

  /* Debug: verify what we just set */
  {
    GtkWidget *dbg_child = gtk_popover_get_child (self->core_label_popover);
    guint      dbg_n_btn = 0;
    if (dbg_child != NULL)
      {
        GtkWidget *dbg_c = gtk_widget_get_first_child (dbg_child);
        while (dbg_c != NULL)
          {
            if (GTK_IS_BUTTON (dbg_c))
              dbg_n_btn++;
            dbg_c = gtk_widget_get_next_sibling (dbg_c);
          }
      }
    {
      GtkPopover *mb_popover = gtk_menu_button_get_popover (
          GTK_MENU_BUTTON (self->core_label_button));
      g_test_message ("DIAG:rebuild-end | popover=%p | mb_popover=%p | same=%d"
                       " | popover_child=%p | n_buttons=%u",
                       (void *) self->core_label_popover,
                       (void *) mb_popover,
                       mb_popover == self->core_label_popover,
                       (void *) dbg_child, dbg_n_btn);
    }
  }
}

static gint
compare_names (gconstpointer a, gconstpointer b)
{
  return g_strcmp0 (*(const char **) a, *(const char **) b);
}

static void on_custom_label_toggled (GtkCheckButton *button, BzFullView *self);

static void rebuild_custom_label_popover (BzFullView *self)
{
  GtkWidget     *box = NULL;
  GtkWidget     *scroll = NULL;
  const char    *current_app_id = NULL;
  GPtrArray     *current_custom_labels = NULL;
  GHashTableIter iter;
  gpointer       k = NULL;
  GtkWidget     *list_box = NULL;

  gtk_popover_set_child (self->custom_label_popover, NULL);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  if (self->group == NULL)
    {
      GtkWidget *no_app = gtk_label_new ("No app selected");
      gtk_widget_set_margin_start (no_app, 12);
      gtk_widget_set_margin_end (no_app, 12);
      gtk_widget_set_margin_top (no_app, 6);
      gtk_widget_set_margin_bottom (no_app, 6);
      gtk_box_append (GTK_BOX (box), no_app);
      gtk_popover_set_child (self->custom_label_popover, box);
      return;
    }

  current_app_id = bz_entry_group_get_id (self->group);
  current_custom_labels = (GPtrArray *) g_hash_table_lookup (self->custom_labels, current_app_id);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_margin_start (box, 8);
  gtk_widget_set_margin_end (box, 8);
  gtk_widget_set_margin_top (box, 8);
  gtk_widget_set_margin_bottom (box, 8);
  gtk_widget_set_size_request (box, 280, -1);

  /* Custom Labels (Multi-Select - Checkboxes) */
  {
    GtkWidget *section_label = gtk_label_new ("Custom Labels");
    gtk_widget_set_halign (section_label, GTK_ALIGN_START);
    gtk_widget_add_css_class (section_label, "heading");
    gtk_widget_set_margin_bottom (section_label, 8);
    gtk_widget_set_margin_start (section_label, 6);
    gtk_box_append (GTK_BOX (box), section_label);
  }

  /* Scrollable list of available custom label names */
  scroll = gtk_scrolled_window_new ();
  gtk_widget_set_vexpand (scroll, TRUE);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_overlay_scrolling (GTK_SCROLLED_WINDOW (scroll), FALSE);
  gtk_scrolled_window_set_propagate_natural_height (GTK_SCROLLED_WINDOW (scroll), TRUE);

  /* Dynamic height: 80% of screen height, max 600px */
  {
    GdkDisplay *display = gtk_widget_get_display (GTK_WIDGET (self));
    GdkSurface *surface = gtk_native_get_surface (GTK_NATIVE (gtk_widget_get_root (GTK_WIDGET (self))));
    GdkMonitor *monitor = gdk_display_get_monitor_at_surface (display, surface);
    if (monitor != NULL)
      {
        GdkRectangle geometry;
        gdk_monitor_get_geometry (monitor, &geometry);
        int max_height = geometry.height * 8 / 10;  // 80%
        if (max_height > 600)
          max_height = 600;
        gtk_scrolled_window_set_max_content_height (GTK_SCROLLED_WINDOW (scroll), max_height);
      }
    else
      gtk_scrolled_window_set_max_content_height (GTK_SCROLLED_WINDOW (scroll), 250);
  }

  list_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  if (g_hash_table_size (self->custom_label_names) == 0)
    {
      GtkWidget *empty = gtk_label_new ("No custom labels defined");
      gtk_widget_set_margin_start (empty, 12);
      gtk_widget_set_margin_end (empty, 12);
      gtk_widget_set_margin_top (empty, 6);
      gtk_widget_set_margin_bottom (empty, 6);
      gtk_box_append (GTK_BOX (list_box), empty);
    }
  else
    {
      GPtrArray *sorted;

      sorted = g_ptr_array_new ();
      g_hash_table_iter_init (&iter, self->custom_label_names);
      while (g_hash_table_iter_next (&iter, &k, NULL))
        g_ptr_array_add (sorted, k);
      g_ptr_array_sort (sorted, compare_names);

      for (guint i = 0; i < sorted->len; i++)
        {
          const char   *name = (const char *) g_ptr_array_index (sorted, i);
          GtkWidget    *check = gtk_check_button_new_with_label (name);
          gboolean      is_selected = FALSE;

          if (current_custom_labels != NULL)
            {
              for (guint j = 0; j < current_custom_labels->len; j++)
                {
                  const char *assigned = (const char *) g_ptr_array_index (current_custom_labels, j);
                  if (g_strcmp0 (assigned, name) == 0)
                    {
                      is_selected = TRUE;
                      break;
                    }
                }
            }

          gtk_check_button_set_active (GTK_CHECK_BUTTON (check), is_selected);
          if (is_selected)
            gtk_widget_add_css_class (check, "suggested-action");
          gtk_widget_set_margin_start (check, 6);
          gtk_widget_set_margin_end (check, 6);
          gtk_widget_set_margin_top (check, 2);
          gtk_widget_set_margin_bottom (check, 2);

          g_object_set_data_full (G_OBJECT (check), "custom-label", g_strdup (name), g_free);
          g_signal_connect (check, "toggled",
                            G_CALLBACK (on_custom_label_toggled), self);
          gtk_box_append (GTK_BOX (list_box), check);
        }
      g_ptr_array_unref (sorted);
    }

  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), list_box);

  gtk_box_append (GTK_BOX (box), scroll);

  gtk_popover_set_child (self->custom_label_popover, box);
}
static void on_full_view_entry_group_changed (BzFullView *self)
{
  rebuild_core_label_popover (self);
  rebuild_custom_label_popover (self);
}

/* Callbacks for label toggles */

static void on_core_label_toggled (GtkCheckButton *button,
                        BzFullView     *self)
{
  const char *label;
  BzEntryGroup *group;
  const char *app_id;

  if (!gtk_check_button_get_active (button))
    return;

  label = (const char *) g_object_get_data (G_OBJECT (button), "core-label");
  if (label == NULL)
    return;

  group = self->group;
  if (group == NULL)
    return;

  app_id = bz_entry_group_get_id (group);
  g_hash_table_insert (self->core_labels,
                       g_strdup (app_id),
                       g_strdup (label));
  bz_label_store_set_core_label (self->label_store, app_id, label, NULL);
  gtk_popover_popdown (GTK_POPOVER (self->core_label_popover));
}

static void on_custom_label_toggled (GtkCheckButton *button,
                          BzFullView     *self)
{
  const char *label;
  BzEntryGroup *group;
  const char *app_id;
  GPtrArray *current_labels;

  label = (const char *) g_object_get_data (G_OBJECT (button), "custom-label");
  if (label == NULL)
    return;

  group = self->group;
  if (group == NULL)
    return;

  app_id = bz_entry_group_get_id (group);

  current_labels = (GPtrArray *) g_hash_table_lookup (self->custom_labels, app_id);
  if (current_labels == NULL)
    {
      current_labels = g_ptr_array_new_with_free_func (g_free);
      g_hash_table_insert (self->custom_labels, g_strdup (app_id), current_labels);
    }

  if (gtk_check_button_get_active (button))
    {
      /* Add label */
      g_ptr_array_add (current_labels, g_strdup (label));
      bz_label_store_add_noncore_label (self->label_store, app_id, label, NULL);
      gtk_widget_add_css_class (GTK_WIDGET (button), "suggested-action");
    }
  else
    {
      /* Remove label */
      for (guint i = 0; i < current_labels->len; i++)
        {
          const char *existing = (const char *) g_ptr_array_index (current_labels, i);
          if (g_strcmp0 (existing, label) == 0)
            {
              g_ptr_array_remove_index (current_labels, i);
              break;
            }
        }
      bz_label_store_remove_noncore_label (self->label_store, app_id, label, NULL);
      gtk_widget_remove_css_class (GTK_WIDGET (button), "suggested-action");
    }
}

/* Public API */

void bz_full_view_update_custom_label_names (BzFullView *self,
                                        GPtrArray  *names)
{
  g_return_if_fail (BZ_IS_FULL_VIEW (self));

  g_hash_table_remove_all (self->custom_label_names);
  if (names != NULL)
    {
      for (guint i = 0; i < names->len; i++)
        {
          const char *name = (const char *) g_ptr_array_index (names, i);
          if (name != NULL && *name != '\0')
            g_hash_table_add (self->custom_label_names, g_strdup (name));
        }
    }

  /* Rebuild popover if it's currently showing */
  if (self->custom_label_popover != NULL &&
      gtk_popover_get_child (self->custom_label_popover) != NULL)
    rebuild_custom_label_popover (self);
}

void bz_full_view_reload_custom_label_names (BzFullView *self)
{
  g_return_if_fail (BZ_IS_FULL_VIEW (self));

  if (self->label_store == NULL)
    return;

  char **names = bz_label_store_get_all_label_names (self->label_store);
  if (names != NULL)
    {
      g_hash_table_remove_all (self->custom_label_names);
      for (guint i = 0; names[i] != NULL; i++)
        g_hash_table_add (self->custom_label_names, g_strdup (names[i]));
      g_strfreev (names);
    }

  /* Rebuild popover if it's currently showing */
  if (self->custom_label_popover != NULL &&
      gtk_popover_get_child (self->custom_label_popover) != NULL)
    rebuild_custom_label_popover (self);
}


static void
bz_full_view_class_init (BzFullViewClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_full_view_dispose;
  object_class->get_property = bz_full_view_get_property;
  object_class->set_property = bz_full_view_set_property;

  props[PROP_STATE] =
      g_param_spec_object (
          "state",
          NULL, NULL,
          BZ_TYPE_STATE_INFO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_ENTRY_GROUP] =
      g_param_spec_object (
          "entry-group",
          NULL, NULL,
          BZ_TYPE_ENTRY_GROUP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_UI_ENTRY] =
      g_param_spec_object (
          "ui-entry",
          NULL, NULL,
          BZ_TYPE_RESULT,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  signals[SIGNAL_UPDATE] =
      g_signal_new (
          "update",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_FIRST,
          0,
          NULL, NULL,
          g_cclosure_marshal_VOID__OBJECT,
          G_TYPE_NONE, 1,
          G_TYPE_LIST_MODEL);
  g_signal_set_va_marshaller (
      signals[SIGNAL_UPDATE],
      G_TYPE_FROM_CLASS (klass),
      g_cclosure_marshal_VOID__OBJECTv);

  g_type_ensure (BZ_TYPE_APPSTREAM_DESCRIPTION_RENDER);
  g_type_ensure (BZ_TYPE_DEVELOPER_BADGE);
  g_type_ensure (BZ_TYPE_DYNAMIC_LIST_VIEW);
  g_type_ensure (BZ_TYPE_ENTRY);
  g_type_ensure (BZ_TYPE_ENTRY_GROUP);
  g_type_ensure (BZ_TYPE_FADING_CLAMP);
  g_type_ensure (BZ_TYPE_FAVORITE_BUTTON);
  g_type_ensure (BZ_TYPE_FLATPAK_ENTRY);
  g_type_ensure (BZ_TYPE_HARDWARE_SUPPORT_DIALOG);
  g_type_ensure (BZ_TYPE_INSTALL_CONTROLS);
  g_type_ensure (BZ_TYPE_SECTION_VIEW);
  g_type_ensure (BZ_TYPE_RELEASES_LIST);
  g_type_ensure (BZ_TYPE_SCREENSHOTS_CAROUSEL);
  g_type_ensure (BZ_TYPE_SHARE_LIST);
  g_type_ensure (BZ_TYPE_CONTEXT_TILE);
  g_type_ensure (BZ_TYPE_ADDON_TILE);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-full-view.ui");
  bz_widget_class_bind_all_util_callbacks (widget_class);
  bz_widget_class_bind_all_context_tile_callbacks (widget_class);
  gtk_widget_class_bind_template_child (widget_class, BzFullView, stack);
  gtk_widget_class_bind_template_child (widget_class, BzFullView, main_scroll);
  gtk_widget_class_bind_template_child (widget_class, BzFullView, shadow_overlay);
  gtk_widget_class_bind_template_child (widget_class, BzFullView, description_toggle);
  gtk_widget_class_bind_template_child (widget_class, BzFullView, core_label_button);
  gtk_widget_class_bind_template_child (widget_class, BzFullView, noncore_label_button);
  gtk_widget_class_bind_template_callback (widget_class, is_scrolled_down);
  gtk_widget_class_bind_template_callback (widget_class, age_rating_cb);
  gtk_widget_class_bind_template_callback (widget_class, format_as_link);
  gtk_widget_class_bind_template_callback (widget_class, has_link);
  gtk_widget_class_bind_template_callback (widget_class, format_leftover_label);
  gtk_widget_class_bind_template_callback (widget_class, format_other_apps_label);
  gtk_widget_class_bind_template_callback (widget_class, format_more_other_apps_label);
  gtk_widget_class_bind_template_callback (widget_class, get_developer_apps_entries);
  gtk_widget_class_bind_template_callback (widget_class, get_dev_apps_max_children_per_line);
  gtk_widget_class_bind_template_callback (widget_class, more_apps_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, open_url_cb);
  gtk_widget_class_bind_template_callback (widget_class, license_cb);
  gtk_widget_class_bind_template_callback (widget_class, dl_stats_cb);
  gtk_widget_class_bind_template_callback (widget_class, size_cb);
  gtk_widget_class_bind_template_callback (widget_class, formfactor_cb);
  gtk_widget_class_bind_template_callback (widget_class, safety_cb);
  gtk_widget_class_bind_template_callback (widget_class, update_cb);
  gtk_widget_class_bind_template_callback (widget_class, delete_user_data_cb);
  gtk_widget_class_bind_template_callback (widget_class, support_cb);
  gtk_widget_class_bind_template_callback (widget_class, pick_license_warning);
  gtk_widget_class_bind_template_callback (widget_class, get_addon_groups);
  gtk_widget_class_bind_template_callback (widget_class, should_show_addon_overflow);
  gtk_widget_class_bind_template_callback (widget_class, install_addons_cb);
  gtk_widget_class_bind_template_callback (widget_class, addon_tile_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, bind_app_tile_cb);
  gtk_widget_class_bind_template_callback (widget_class, unbind_app_tile_cb);
  gtk_widget_class_bind_template_callback (widget_class, get_description_max_height);
  gtk_widget_class_bind_template_callback (widget_class, get_description_toggle_text);
  gtk_widget_class_bind_template_callback (widget_class, metainfo_banner_visible);
  gtk_widget_class_bind_template_callback (widget_class, preview_other_metainfo_cb);
  gtk_widget_class_bind_template_callback (widget_class, copy_id_cb);
  gtk_widget_class_bind_template_callback (widget_class, debug_id_inspect_cb);
}

static void
bz_full_view_init (BzFullView *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

/* Core label tracking (shared with custom companion app) */
  self->core_labels         = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                      g_free, g_free);
  self->core_label_names    = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                      g_free, NULL);
  self->custom_labels       = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                      g_free,
                                                      (GDestroyNotify) g_ptr_array_unref);
  self->custom_label_names  = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                      g_free, NULL);
  {
    char *data_dir = g_build_filename (g_get_user_data_dir (),
                                       "io.github.kolunmi.Bazaar",
                                       NULL);
    char *db_path  = g_build_filename (data_dir, "custom-labels.db", NULL);

    self->label_store = bz_label_store_open (db_path, data_dir, NULL);
    g_free (db_path);
    g_free (data_dir);
  }
  /* Use default label if store missing — load_label_maps handles it */
  load_label_maps (self);

  /* Core label popover */
  self->core_label_popover = GTK_POPOVER (gtk_popover_new ());
  gtk_menu_button_set_popover (GTK_MENU_BUTTON (self->core_label_button),
                               GTK_WIDGET (self->core_label_popover));

  /* Rebuild popover content before the button opens it (capture phase) */
  {
    GtkGesture *gesture = gtk_gesture_click_new ();
    gtk_event_controller_set_propagation_phase (
        GTK_EVENT_CONTROLLER (gesture), GTK_PHASE_CAPTURE);
    g_signal_connect_swapped (gesture, "pressed",
                              G_CALLBACK (rebuild_core_label_popover), self);
    gtk_widget_add_controller (GTK_WIDGET (self->core_label_button),
                               GTK_EVENT_CONTROLLER (gesture));
  }

  /* Custom label popover */
  self->custom_label_popover = GTK_POPOVER (gtk_popover_new ());
  gtk_menu_button_set_popover (GTK_MENU_BUTTON (self->noncore_label_button),
                               GTK_WIDGET (self->custom_label_popover));

  /* Rebuild popover content before the button opens it (capture phase) */
  {
    GtkGesture *gesture = gtk_gesture_click_new ();
    gtk_event_controller_set_propagation_phase (
        GTK_EVENT_CONTROLLER (gesture), GTK_PHASE_CAPTURE);
    g_signal_connect_swapped (gesture, "pressed",
                              G_CALLBACK (rebuild_custom_label_popover), self);
    gtk_widget_add_controller (GTK_WIDGET (self->noncore_label_button),
                               GTK_EVENT_CONTROLLER (gesture));
  }

  /* Rebuild popover when entry group changes */
  g_signal_connect_swapped (self, "notify::entry-group",
                            G_CALLBACK (on_full_view_entry_group_changed), self);
}

GtkWidget *
bz_full_view_new (void)
{
  return g_object_new (BZ_TYPE_FULL_VIEW, NULL);
}

static DexFuture *
on_ui_entry_resolved (DexFuture *future,
                      GWeakRef  *wr)
{
  g_autoptr (BzFullView) self         = NULL;
  BzEntry *ui_entry                   = NULL;
  g_autoptr (BzResult) runtime_result = NULL;
  const GValue *value                 = NULL;

  bz_weak_get_or_return_reject (self, wr);

  value = dex_future_get_value (future, NULL);
  if (value != NULL && G_VALUE_HOLDS_OBJECT (value))
    {
      ui_entry = g_value_get_object (value);

      if (BZ_IS_FLATPAK_ENTRY (ui_entry))
        self->runtime = bz_flatpak_entry_dup_runtime_result (BZ_FLATPAK_ENTRY (ui_entry));
    }

  adw_view_stack_set_visible_child_name (self->stack, "content");

  return dex_future_new_for_boolean (TRUE);
}

void
bz_full_view_set_entry_group (BzFullView   *self,
                              BzEntryGroup *group)
{
  g_return_if_fail (BZ_IS_FULL_VIEW (self));
  g_return_if_fail (group == NULL ||
                    BZ_IS_ENTRY_GROUP (group));

  if (group == self->group)
    return;

  dex_clear (&self->ui_future);
  g_clear_object (&self->group);
  g_clear_object (&self->ui_entry);
  g_clear_object (&self->runtime);
  g_clear_object (&self->group_model);
  gtk_toggle_button_set_active (self->description_toggle, FALSE);

  if (group != NULL)
    {
      self->group    = g_object_ref (group);
      self->ui_entry = bz_entry_group_dup_ui_entry (group);

      if (self->ui_entry != NULL && bz_result_get_resolved (self->ui_entry))
        {
          BzEntry *entry                      = NULL;
          g_autoptr (GListStore) store        = NULL;
          g_autoptr (DexFuture) future        = NULL;
          g_autoptr (DexFuture) object_future = NULL;
          GWeakRef *wr                        = NULL;

          entry = bz_result_get_object (self->ui_entry);
          store = g_list_store_new (BZ_TYPE_ENTRY);
          g_list_store_append (store, entry);

          future            = dex_future_new_for_object (store);
          self->group_model = bz_result_new (future);

          object_future = dex_future_new_for_object (entry);
          wr            = bz_track_weak (self);
          dex_unref (on_ui_entry_resolved (object_future, wr));
          bz_weak_release (wr);
        }
      else
        {
          g_autoptr (DexFuture) future = NULL;

          future            = bz_entry_group_dup_all_into_store (group);
          self->group_model = bz_result_new (future);

          if (self->ui_entry != NULL)
            {
              g_autoptr (DexFuture) ui_future = NULL;

              adw_view_stack_set_visible_child_name (self->stack, "loading");

              ui_future = bz_result_dup_future (self->ui_entry);
              ui_future = dex_future_then (
                  ui_future,
                  (DexFutureCallback) on_ui_entry_resolved,
                  bz_track_weak (self),
                  bz_weak_release);
              self->ui_future = g_steal_pointer (&ui_future);
            }
        }
    }
  else
    adw_view_stack_set_visible_child_name (self->stack, "empty");

  gtk_adjustment_set_value (gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->main_scroll)), 0.0);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ENTRY_GROUP]);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_UI_ENTRY]);
}

BzEntryGroup *
bz_full_view_get_entry_group (BzFullView *self)
{
  g_return_val_if_fail (BZ_IS_FULL_VIEW (self), NULL);
  return self->group;
}
