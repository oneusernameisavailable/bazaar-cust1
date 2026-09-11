/* cz-custom-page.c
 *
 * Copyright 2025
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

#include "config.h"

#include "cz-custom-page.h"

#include <glib/gi18n.h>

#include "bz-app-tile.h"
#include "bz-application.h"
#include "bz-window.h"
#include "bz-dynamic-list-view.h"
#include "bz-entry-group.h"
#include "bz-flathub-state.h"
#include "bz-state-info.h"
#include "cz-custom-filter.h"
#include "cz-custom-label-store.h"

static void
trace (const char *fmt, ...) __attribute__((format (printf, 1, 2)));

static void
trace (const char *fmt, ...)
{
  va_list ap;
  va_start (ap, fmt);
  g_logv (G_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, fmt, ap);
  va_end (ap);
}

struct _CzCustomPage
{
  AdwBin parent_instance;

  BzStateInfo         *state;
  CzCustomLabelStore  *label_store;
  char                *label_store_path;

  GtkWidget *apps_list_view;
  GtkWidget *selected_tile;       /* category pill currently selected */
  GtkWidget *selected_core_tile;  /* core label pill currently selected */
  GtkWidget *pill_box;            /* row 1: category pills */
  GtkWidget *custom_pill_box;     /* row 2: core label pills (New/Install/4-Stars/3-Stars/Forget it) */
  GtkWidget *noncore_pill_box;     /* row 3: non-core label pills (Test1, Test2, ...) */

  GtkWidget    *scroll;            /* main scrolled window (populate_categories) */
  GtkRevealer  *to_top_revealer;   /* floating back-to-top button */

  GListModel *all_groups;
  gulong      all_groups_changed_id;

  /* Snapshot of the combined filter taken when the user clicks an app tile,
   * restored on ::map when returning from the full-view back button. */
  gboolean   restore_on_map;
  char      *restore_category;    /* internal category name (cz-category) */
  char      *restore_core_label;  /* selected core-label pill text */
  GPtrArray *restore_noncore;     /* selected non-core label names */
};

G_DEFINE_FINAL_TYPE (CzCustomPage, cz_custom_page, ADW_TYPE_BIN)

static void
snapshot_filter (CzCustomPage *self);

static void
tile_clicked (BzEntryGroup *group,
              GtkButton    *button)
{
  CzCustomPage *self = CZ_CUSTOM_PAGE (g_object_get_data (
      G_OBJECT (button), "cz-custom-page"));

  if (self != NULL)
    snapshot_filter (self);

  gtk_widget_activate_action (GTK_WIDGET (button), "window.show-group", "s",
                              bz_entry_group_get_id (group));
}

static void
bind_widget_cb (BzDynamicListView *view,
                GtkWidget         *tile,
                BzEntryGroup      *group,
                gpointer           user_data)
{
  g_object_set_data (G_OBJECT (tile), "cz-custom-page",
                     g_object_get_data (G_OBJECT (view), "cz-custom-page"));
  g_signal_connect_swapped (tile, "clicked", G_CALLBACK (tile_clicked), group);
}

static void
unbind_widget_cb (BzDynamicListView *view,
                  GtkWidget         *tile,
                  BzEntryGroup      *group,
                  gpointer           user_data)
{
  g_signal_handlers_disconnect_by_func (tile, G_CALLBACK (tile_clicked), group);
}

static void
to_top_clicked_cb (CzCustomPage *self)
{
  GtkAdjustment *vadj;

  g_return_if_fail (CZ_IS_CUSTOM_PAGE (self));
  if (self->scroll == NULL)
    return;

  vadj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scroll));
  trace ("to_top_clicked_cb: scrolling to top (value=%.1f)",
         gtk_adjustment_get_value (vadj));
  gtk_adjustment_set_value (vadj, gtk_adjustment_get_lower (vadj));
}

static void
on_scroll_changed_cb (CzCustomPage *self)
{
  GtkAdjustment *vadj;
  gboolean       reveal;

  g_return_if_fail (CZ_IS_CUSTOM_PAGE (self));
  if (self->scroll == NULL || self->to_top_revealer == NULL)
    return;

  vadj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scroll));
  reveal = gtk_adjustment_get_value (vadj) > 0.0;
  trace ("on_scroll_changed_cb: value=%.1f reveal=%d",
         gtk_adjustment_get_value (vadj), reveal);
  gtk_revealer_set_reveal_child (self->to_top_revealer, reveal);
}

/* ---- Filter context for label-aware filtering ---- */

typedef struct
{
  CzCustomLabelStore *store;
  GHashTable         *id_set;
  char               *core_label;
  GPtrArray          *noncore;  /* array of char*, or NULL */
} CzFilterCtx;

static void
cz_filter_ctx_free (gpointer data)
{
  CzFilterCtx *ctx = (CzFilterCtx *) data;

  if (ctx == NULL)
    return;
  g_hash_table_unref (ctx->id_set);
  g_free (ctx->core_label);
  if (ctx->noncore != NULL)
    {
      guint i;

      for (i = 0; i < ctx->noncore->len; i++)
        g_free (g_ptr_array_index (ctx->noncore, i));
      g_ptr_array_unref (ctx->noncore);
    }
  g_free (ctx);
}

static gboolean
cz_label_filter_func (gpointer item, gpointer user_data)
{
  CzFilterCtx  *ctx = (CzFilterCtx *) user_data;
  const char   *app_id;

  if (item == NULL || ctx == NULL || ctx->id_set == NULL)
    return FALSE;
  if (!BZ_IS_ENTRY_GROUP (item))
    return FALSE;

  app_id = bz_entry_group_get_id (BZ_ENTRY_GROUP (item));
  if (app_id == NULL)
    return FALSE;

  /* 1. Check category membership */
  if (!g_hash_table_contains (ctx->id_set, app_id))
    return FALSE;

  /* 2. Check core label */
  if (ctx->core_label != NULL)
    {
      const char *actual = cz_custom_label_store_get_core_label (ctx->store, app_id);

      if (g_strcmp0 (actual, ctx->core_label) != 0)
        return FALSE;
    }

  /* 3. Check non-core labels (AND: must have ALL selected) */
  if (ctx->noncore != NULL && ctx->noncore->len > 0)
    {
      guint i;

      for (i = 0; i < ctx->noncore->len; i++)
        {
          const char *label = (const char *) g_ptr_array_index (ctx->noncore, i);

          if (!cz_custom_label_store_has_noncore_label (ctx->store, app_id, label))
            return FALSE;
        }
    }

  return TRUE;
}

/* Collect selected non-core label names into a new GPtrArray (caller frees) */
static GPtrArray *
collect_selected_noncore (CzCustomPage *self)
{
  GPtrArray *arr;
  GtkWidget *child;

  if (self->noncore_pill_box == NULL)
    return NULL;

  arr = g_ptr_array_new_with_free_func (g_free);
  child = gtk_widget_get_first_child (self->noncore_pill_box);
  while (child != NULL)
    {
      if (GTK_IS_BUTTON (child) &&
          gtk_widget_has_css_class (child, "selected"))
        {
          const char *label = gtk_button_get_label (GTK_BUTTON (child));

          if (label != NULL)
            g_ptr_array_add (arr, g_strdup (label));
        }
      child = gtk_widget_get_next_sibling (child);
    }
  return arr;
}

/* Free the current filter snapshot (fields may be re-snapshotted). */
static void
clear_filter_snapshot (CzCustomPage *self)
{
  g_clear_pointer (&self->restore_category, g_free);
  g_clear_pointer (&self->restore_core_label, g_free);
  g_clear_pointer (&self->restore_noncore, g_ptr_array_unref);
}

/* Record the combined filter exactly as it is right now (used right before
 * navigating into an app's full view, so the back button can reapply it). */
static void
snapshot_filter (CzCustomPage *self)
{
  BzFlathubCategory *category;

  g_return_if_fail (CZ_IS_CUSTOM_PAGE (self));

  clear_filter_snapshot (self);

  if (self->selected_tile != NULL)
    {
      category = g_object_get_data (G_OBJECT (self->selected_tile),
                                    "cz-category");
      if (category != NULL)
        self->restore_category = g_strdup (
            bz_flathub_category_get_name (category));
    }

  if (self->selected_core_tile != NULL)
    self->restore_core_label = g_strdup (
        gtk_button_get_label (GTK_BUTTON (self->selected_core_tile)));

  self->restore_noncore = collect_selected_noncore (self);

  trace ("snapshot_filter: category=%s core=%s noncore=%u",
         self->restore_category ? self->restore_category : "(null)",
         self->restore_core_label ? self->restore_core_label : "(null)",
         self->restore_noncore ? self->restore_noncore->len : 0);

  self->restore_on_map = TRUE;
}

/* Find the category pill whose cz-category object has the given name. */
static GtkWidget *
find_category_pill_by_name (CzCustomPage *self,
                            const char   *cat_name)
{
  GtkWidget *child;

  if (self->pill_box == NULL || cat_name == NULL)
    return NULL;

  child = gtk_widget_get_first_child (self->pill_box);
  while (child != NULL)
    {
      BzFlathubCategory *category = g_object_get_data (G_OBJECT (child),
                                                       "cz-category");

      if (category != NULL &&
          g_strcmp0 (bz_flathub_category_get_name (category), cat_name) == 0)
        return child;
      child = gtk_widget_get_next_sibling (child);
    }
  return NULL;
}

/* Find a pill button by label text among the direct children of box. */
static GtkWidget *
find_pill_by_label (GtkWidget   *box,
                    const char  *label)
{
  GtkWidget *child;

  if (box == NULL || label == NULL)
    return NULL;

  child = gtk_widget_get_first_child (box);
  while (child != NULL)
    {
      if (GTK_IS_BUTTON (child) &&
          g_strcmp0 (gtk_button_get_label (GTK_BUTTON (child)), label) == 0)
        return child;
      child = gtk_widget_get_next_sibling (child);
    }
  return NULL;
}

/* Re-apply the snapshot on return from the full view: re-mark the recorded
 * category / core / non-core pills and re-run the filter.  Returns TRUE if a
 * valid category was restored, FALSE to fall back to the default reset. */
static void
apply_filter (CzCustomPage      *self,
              BzFlathubCategory *category);

static gboolean
restore_filter (CzCustomPage *self)
{
  BzFlathubCategory *category  = NULL;
  GtkWidget         *cat_pill  = NULL;
  GtkWidget         *core_pill = NULL;
  GtkWidget         *child;

  trace ("restore_filter: category=%s core=%s noncore=%u",
         self->restore_category ? self->restore_category : "(null)",
         self->restore_core_label ? self->restore_core_label : "(null)",
         self->restore_noncore ? self->restore_noncore->len : 0);

  cat_pill = find_category_pill_by_name (self, self->restore_category);
  if (cat_pill == NULL)
    {
      trace ("restore_filter: category pill not found, falling back to reset");
      return FALSE;
    }

  /* Category: mono-select the restored pill */
  child = gtk_widget_get_first_child (self->pill_box);
  while (child != NULL)
    {
      if (GTK_IS_BUTTON (child))
        gtk_widget_remove_css_class (child, "selected");
      child = gtk_widget_get_next_sibling (child);
    }
  gtk_widget_add_css_class (cat_pill, "selected");
  self->selected_tile = cat_pill;
  gtk_widget_set_visible (self->apps_list_view, TRUE);
  category = g_object_get_data (G_OBJECT (cat_pill), "cz-category");

  /* Core label: mono-select the restored pill if it still exists,
   * otherwise leave no core constraint. */
  if (self->custom_pill_box != NULL)
    {
      for (child = gtk_widget_get_first_child (self->custom_pill_box);
           child != NULL; child = gtk_widget_get_next_sibling (child))
        if (GTK_IS_BUTTON (child))
          gtk_widget_remove_css_class (child, "selected");
      self->selected_core_tile = NULL;

      core_pill = find_pill_by_label (self->custom_pill_box,
                                      self->restore_core_label);
      if (core_pill != NULL)
        {
          gtk_widget_add_css_class (core_pill, "selected");
          self->selected_core_tile = core_pill;
        }
    }

  /* Non-core: re-mark each recorded label pill that still exists */
  if (self->noncore_pill_box != NULL && self->restore_noncore != NULL)
    {
      GtkWidget *npill;

      for (guint i = 0; i < self->restore_noncore->len; i++)
        {
          npill = find_pill_by_label (self->noncore_pill_box,
              g_ptr_array_index (self->restore_noncore, i));
          if (npill != NULL)
            gtk_widget_add_css_class (npill, "selected");
        }
    }

  apply_filter (self, category);
  return TRUE;
}

static void
apply_filter (CzCustomPage     *self,
              BzFlathubCategory *category)
{
  GHashTable        *id_set    = NULL;
  CzFilterCtx       *ctx       = NULL;
  GtkFilter         *filter    = NULL;
  GListModel        *all       = NULL;
  GtkFilterListModel *filtered = NULL;
  const char        *cat_name;

  cat_name = category ? bz_flathub_category_get_name (category) : "NULL";
  trace ("apply_filter: category=%s, apps_list_view=%p",
         cat_name, self->apps_list_view);

  /* Base model: always use entry groups (the filter checks
   * BZ_IS_ENTRY_GROUP).  The id_set built from the category's
   * applications list handles category membership. */
  all = bz_state_info_get_all_entry_groups (self->state);

  if (all == NULL)
    {
      trace ("apply_filter: base model is NULL");
      return;
    }

  id_set = cz_category_build_id_set (category);

  ctx = g_new0 (CzFilterCtx, 1);
  ctx->store = self->label_store;
  ctx->id_set = id_set;
  if (self->selected_core_tile != NULL)
    ctx->core_label = g_strdup (gtk_button_get_label (
        GTK_BUTTON (self->selected_core_tile)));
  ctx->noncore = collect_selected_noncore (self);

  filter = GTK_FILTER (gtk_custom_filter_new (
      (GtkCustomFilterFunc) cz_label_filter_func,
      ctx, cz_filter_ctx_free));
  filtered = gtk_filter_list_model_new (all, filter);
  trace ("apply_filter: filtered model created (%p), setting on list view (%p)",
         filtered, self->apps_list_view);
  bz_dynamic_list_view_set_model (
      BZ_DYNAMIC_LIST_VIEW (self->apps_list_view),
      G_LIST_MODEL (filtered));
  {
    guint n_filtered = g_list_model_get_n_items (G_LIST_MODEL (filtered));
    trace (
        "apply_filter: category='%s' id_set_size=%u filtered_count=%u",
        cat_name, id_set ? g_hash_table_size (id_set) : 0, n_filtered);
  }
  trace ("apply_filter: model set on list view");
}

static void
category_selected (CzCustomPage *self,
                   GtkWidget    *pill)
{
  BzFlathubCategory *category = NULL;

  trace ("category_selected called, pill=%p", pill);

  category = g_object_get_data (G_OBJECT (pill), "cz-category");
  if (category == NULL)
    {
      trace ("category_selected: category is NULL");
      return;
    }

  trace ("category_selected: category=%s", bz_flathub_category_get_name (category));

  if (self->selected_tile != NULL)
    gtk_widget_remove_css_class (self->selected_tile, "selected");
  gtk_widget_add_css_class (pill, "selected");
  self->selected_tile = pill;
  gtk_widget_set_visible (self->apps_list_view, TRUE);

  apply_filter (self, category);
}

static void
noncore_label_selected (CzCustomPage *self,
                         GtkWidget    *pill)
{
  BzFlathubCategory *category = NULL;
  const char        *label;

  label = gtk_button_get_label (GTK_BUTTON (pill));
  trace ("noncore_label_selected: label=%s", label);

  /* Toggle */
  if (gtk_widget_has_css_class (pill, "selected"))
    gtk_widget_remove_css_class (pill, "selected");
  else
    gtk_widget_add_css_class (pill, "selected");

  /* Re-filter with current selections */
  if (self->selected_tile != NULL)
    {
      category = g_object_get_data (G_OBJECT (self->selected_tile), "cz-category");
      if (category != NULL)
        apply_filter (self, category);
    }
}

static void
trigger_trending_click (CzCustomPage *self);

static void
on_entry_groups_changed (GListModel    *model,
                           guint          position,
                           guint          removed,
                           guint          added,
                           CzCustomPage  *self)
{
  trace ("on_entry_groups_changed: position=%u, removed=%u, added=%u, self->selected_tile=%p",
         position, removed, added, self->selected_tile);

  if (added == 0)
    return;

  if (self->selected_tile != NULL)
    {
      BzFlathubCategory *cat = g_object_get_data (
          G_OBJECT (self->selected_tile), "cz-category");
      if (cat != NULL)
        {
          trace ("on_entry_groups_changed: re-applying filter for category=%s", bz_flathub_category_get_name (cat));
          apply_filter (self, cat);
        }
    }
  else
    {
      trace ("on_entry_groups_changed: no selected tile, clicking trending");
      trigger_trending_click (self);
    }
}

static void
on_map (CzCustomPage *self)
{
  GtkWidget *child;

  trace ("on_map called, pill_box=%p", self->pill_box);

  if (self->pill_box == NULL)
    return;                    /* UI not built yet */

  /* Reload label store to pick up changes made from the app detail page */
  if (self->label_store != NULL && self->label_store_path != NULL)
    cz_custom_label_store_load_from_path (self->label_store,
                                            self->label_store_path);

  /* Rebuild noncore pills to reflect any changes */
  cz_custom_page_rebuild_noncore_pills (self);

  /* Return from an app's full view (back button): reapply the combined
   * filter recorded just before entering.  Tab switches never set
   * restore_on_map, so they keep the default reset below. */
  if (self->restore_on_map)
    {
      self->restore_on_map = FALSE;

      if (self->restore_category != NULL && restore_filter (self))
        return;
    }

  /* Auto-select "New" pill by default BEFORE running the filter so that
   * the core-label constraint is applied on first render (monoselect). */
  if (self->custom_pill_box != NULL)
    {
      child = gtk_widget_get_first_child (self->custom_pill_box);
      while (child != NULL)
        {
          if (GTK_IS_BUTTON (child) &&
              g_strcmp0 (gtk_button_get_label (GTK_BUTTON (child)), "New") == 0)
            {
              GtkWidget *sib;

              for (sib = gtk_widget_get_first_child (self->custom_pill_box);
                   sib != NULL; sib = gtk_widget_get_next_sibling (sib))
                if (GTK_IS_BUTTON (sib) && sib != child)
                  gtk_widget_remove_css_class (sib, "selected");

              gtk_widget_add_css_class (child, "selected");
              self->selected_core_tile = child;
              break;
            }
          child = gtk_widget_get_next_sibling (child);
        }
    }

  trigger_trending_click (self);
}

static void
trigger_trending_click (CzCustomPage *self)
{
  GtkWidget *child;

  trace ("trigger_trending_click: pill_box=%p", self->pill_box);

  if (self->pill_box == NULL)
    {
      trace ("trigger_trending_click: pill_box is NULL, returning");
      return;
    }

  child = gtk_widget_get_first_child (self->pill_box);
  while (child != NULL)
    {
      BzFlathubCategory *category = g_object_get_data (G_OBJECT (child),
                                                        "cz-category");
      if (category != NULL &&
          g_strcmp0 (bz_flathub_category_get_name (category), "trending") == 0)
        {
          trace ("trigger_trending_click: emitting 'clicked' on trending pill");
          g_signal_emit_by_name (child, "clicked");
          return;
        }
      child = gtk_widget_get_next_sibling (child);
    }
}

static void
custom_label_selected (CzCustomPage *self,
                       GtkWidget    *pill)
{
  BzFlathubCategory *category = NULL;
  const char        *label;
  GtkWidget         *child;

  label = gtk_button_get_label (GTK_BUTTON (pill));
  trace ("custom_label_selected: label=%s", label);

  /* Monoselect: deselect all other core pills */
  if (self->custom_pill_box != NULL)
    {
      child = gtk_widget_get_first_child (self->custom_pill_box);
      while (child != NULL)
        {
          if (GTK_IS_BUTTON (child) && child != pill)
            gtk_widget_remove_css_class (child, "selected");
          child = gtk_widget_get_next_sibling (child);
        }
    }

  gtk_widget_add_css_class (pill, "selected");
  self->selected_core_tile = pill;

  /* Re-filter with current selections */
  if (self->selected_tile != NULL)
    {
      category = g_object_get_data (G_OBJECT (self->selected_tile), "cz-category");
      if (category != NULL)
        apply_filter (self, category);
    }
}

static void
populate_categories (CzCustomPage *self);

static void
setup_all_groups (CzCustomPage *self)
{
  GListModel *all_groups = bz_state_info_get_all_entry_groups (self->state);
  trace ("setup_all_groups called, all_groups=%p, pill_box=%p, apps_list_view=%p",
         all_groups,
         self->pill_box,
         self->apps_list_view);
  if (all_groups == NULL)
    return;

  self->all_groups = all_groups;
  self->all_groups_changed_id = g_signal_connect (
      self->all_groups, "items-changed",
      G_CALLBACK (on_entry_groups_changed), self);

  if (self->selected_tile != NULL)
    {
      BzFlathubCategory *cat = g_object_get_data (
          G_OBJECT (self->selected_tile), "cz-category");
      if (cat != NULL)
        apply_filter (self, cat);
    }
}

static void
on_all_entry_groups_loaded (CzCustomPage *self,
                            GParamSpec   *pspec,
                            BzStateInfo  *state)
{
  GListModel *all_groups = bz_state_info_get_all_entry_groups (state);
  trace ("on_all_entry_groups_loaded called, all_groups=%p, pspec=%s", all_groups, pspec ? g_param_spec_get_name (pspec) : "NULL");

  if (all_groups == NULL)
    return;  /* Wait for it to become non-NULL */

  g_signal_handlers_disconnect_by_func (state, on_all_entry_groups_loaded, self);
  setup_all_groups (self);

  /* Ensure all apps have a core label now that the full set is available */
  {
    guint n_all = g_list_model_get_n_items (all_groups);
    GPtrArray *all_ids = g_ptr_array_new ();

    for (guint i = 0; i < n_all; i++)
      {
        g_autoptr (GObject) obj = g_list_model_get_item (all_groups, i);

        if (obj != NULL && BZ_IS_ENTRY_GROUP (obj))
          {
            const char *id = bz_entry_group_get_id (BZ_ENTRY_GROUP (obj));
            if (id != NULL)
              g_ptr_array_add (all_ids, (gpointer) id);
          }
      }

    trace (
        "on_all_entry_groups_loaded: all_entry_groups has %u items, extracted %u IDs",
        n_all, all_ids->len);
    if (all_ids->len > 0)
      cz_custom_label_store_ensure_app_ids (
          self->label_store,
          (const char * const *) all_ids->pdata,
          all_ids->len);

    g_ptr_array_unref (all_ids);
  }
}

static void
on_categories_loaded (CzCustomPage   *self,
                      GParamSpec     *pspec,
                      BzFlathubState *flathub)
{
  g_signal_handlers_disconnect_by_func (flathub, on_categories_loaded, self);
  populate_categories (self);
}

static void
populate_categories (CzCustomPage *self)
{
  BzFlathubState  *flathub = NULL;
  GListModel      *categories = NULL;
  GtkWidget       *scroll = NULL;
  GtkWidget       *clamp = NULL;
  GtkWidget       *viewport = NULL;
  GtkWidget       *box = NULL;
  GtkWidget       *label = NULL;

  trace ("populate_categories called");

  if (self->pill_box != NULL)
    {
      trace ("populate_categories: already built (pill_box non-NULL), skipping");
      return;
    }

  flathub = bz_state_info_get_flathub (self->state);
  if (flathub == NULL)
    return;

  categories = bz_flathub_state_get_categories (flathub);
  trace ("populate_categories: categories=%p", categories);
  if (categories == NULL)
    {
      trace ("populate_categories: categories is NULL, connecting notify");
      g_signal_connect_swapped (flathub, "notify::categories",
                                G_CALLBACK (on_categories_loaded), self);
      return;
    }

  trace ("populate_categories: building UI");

  scroll = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_add_css_class (scroll, "transparent");
  self->scroll = scroll;

  clamp = adw_clamp_scrollable_new ();
  adw_clamp_scrollable_set_maximum_size (ADW_CLAMP_SCROLLABLE (clamp), 1500);
  adw_clamp_scrollable_set_tightening_threshold (ADW_CLAMP_SCROLLABLE (clamp), 1400);

  viewport = gtk_viewport_new (NULL, NULL);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 15);
  gtk_widget_set_margin_start (box, 30);
  gtk_widget_set_margin_end (box, 30);
  gtk_widget_set_margin_top (box, 16);
  gtk_widget_set_margin_bottom (box, 50);

  label = gtk_label_new (_("Browse Categories"));
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_widget_set_margin_bottom (label, 12);
  gtk_widget_set_margin_start (label, 4);
  gtk_widget_add_css_class (label, "heading");
  gtk_box_append (GTK_BOX (box), label);

  {
    GtkLayoutManager  *layout = NULL;
    GtkCustomFilter   *filter = NULL;
    GtkFilterListModel *filtered = NULL;
    guint              i;
    guint              n_items;

    self->pill_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    layout = GTK_LAYOUT_MANAGER (adw_wrap_layout_new ());
    adw_wrap_layout_set_child_spacing (ADW_WRAP_LAYOUT (layout), 10);
    adw_wrap_layout_set_line_spacing (ADW_WRAP_LAYOUT (layout), 8);
    adw_wrap_layout_set_justify (ADW_WRAP_LAYOUT (layout), ADW_JUSTIFY_FILL);
    gtk_widget_set_layout_manager (self->pill_box, layout);

    filter = gtk_custom_filter_new (
        (GtkCustomFilterFunc) cz_category_get_show_in_list,
        NULL, NULL);
    filtered = gtk_filter_list_model_new (
        g_object_ref (categories), GTK_FILTER (filter));

    n_items = g_list_model_get_n_items (G_LIST_MODEL (filtered));
    for (i = 0; i < n_items; i++)
      {
        g_autoptr (BzFlathubCategory) category = g_list_model_get_item (
            G_LIST_MODEL (filtered), i);
        const char *pill_label;
        GtkWidget  *pill;

        if (category == NULL)
          continue;

        pill_label = bz_flathub_category_get_display_name (category);
        if (g_strcmp0 (bz_flathub_category_get_name (category), "graphics") == 0)
          pill_label = "Graph and Photo";
        if (pill_label == NULL)
          continue;

        pill = gtk_button_new_with_label (pill_label);
        gtk_widget_add_css_class (pill, "small-pill");
        gtk_widget_add_css_class (pill, "search-pill");
        g_object_set_data (G_OBJECT (pill), "cz-category", category);
        g_signal_connect_swapped (pill, "clicked",
                                  G_CALLBACK (category_selected), self);
        gtk_box_append (GTK_BOX (self->pill_box), pill);
      }

    gtk_box_append (GTK_BOX (box), self->pill_box);
  }

  /* Row 2: Core label pills (monoselect: New / Install / 4-Stars / 3-Stars / Forget it) */
  {
    GtkLayoutManager *custom_layout;
    const char *custom_labels[] = { "New", "Install", "4-Stars", "3-Stars", "Forget it", NULL };

    self->custom_pill_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_top (self->custom_pill_box, 16);
    custom_layout = GTK_LAYOUT_MANAGER (adw_wrap_layout_new ());
    adw_wrap_layout_set_child_spacing (ADW_WRAP_LAYOUT (custom_layout), 10);
    adw_wrap_layout_set_line_spacing (ADW_WRAP_LAYOUT (custom_layout), 8);
    adw_wrap_layout_set_justify (ADW_WRAP_LAYOUT (custom_layout), ADW_JUSTIFY_FILL);
    gtk_widget_set_layout_manager (self->custom_pill_box, custom_layout);

    for (guint j = 0; custom_labels[j] != NULL; j++)
      {
        GtkWidget *pill = gtk_button_new_with_label (custom_labels[j]);
        gtk_widget_add_css_class (pill, "small-pill");
        gtk_widget_add_css_class (pill, "custom-pill");
        g_signal_connect_swapped (pill, "clicked",
                                  G_CALLBACK (custom_label_selected), self);
        gtk_box_append (GTK_BOX (self->custom_pill_box), pill);
      }

    gtk_box_append (GTK_BOX (box), self->custom_pill_box);
  }

  /* Row 3: Non-core label pills (multi-select toggle, from store) */
  {
    GtkLayoutManager *noncore_layout;

    self->noncore_pill_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_top (self->noncore_pill_box, 16);
    noncore_layout = GTK_LAYOUT_MANAGER (adw_wrap_layout_new ());
    adw_wrap_layout_set_child_spacing (ADW_WRAP_LAYOUT (noncore_layout), 10);
    adw_wrap_layout_set_line_spacing (ADW_WRAP_LAYOUT (noncore_layout), 8);
    adw_wrap_layout_set_justify (ADW_WRAP_LAYOUT (noncore_layout), ADW_JUSTIFY_FILL);
    gtk_widget_set_layout_manager (self->noncore_pill_box, noncore_layout);

    cz_custom_page_rebuild_noncore_pills (self);

    gtk_box_append (GTK_BOX (box), self->noncore_pill_box);
  }

  self->apps_list_view = GTK_WIDGET (bz_dynamic_list_view_new ());
  g_object_set_data (G_OBJECT (self->apps_list_view), "cz-custom-page", self);
  bz_dynamic_list_view_set_scroll (BZ_DYNAMIC_LIST_VIEW (self->apps_list_view),
                                   FALSE);
  bz_dynamic_list_view_set_noscroll_kind (
      BZ_DYNAMIC_LIST_VIEW (self->apps_list_view),
      BZ_DYNAMIC_LIST_VIEW_KIND_FLOW_BOX);
  bz_dynamic_list_view_set_child_type (
      BZ_DYNAMIC_LIST_VIEW (self->apps_list_view), "BzAppTile");
  bz_dynamic_list_view_set_child_prop (
      BZ_DYNAMIC_LIST_VIEW (self->apps_list_view), "group");
  bz_dynamic_list_view_set_max_children_per_line (
      BZ_DYNAMIC_LIST_VIEW (self->apps_list_view), 4);
  gtk_widget_set_visible (self->apps_list_view, FALSE);
  gtk_widget_set_valign (self->apps_list_view, GTK_ALIGN_START);
  gtk_widget_set_margin_top (self->apps_list_view, 12);

  g_signal_connect (self->apps_list_view, "bind-widget",
                    G_CALLBACK (bind_widget_cb), NULL);
  g_signal_connect (self->apps_list_view, "unbind-widget",
                    G_CALLBACK (unbind_widget_cb), NULL);

  gtk_box_append (GTK_BOX (box), self->apps_list_view);

  gtk_viewport_set_child (GTK_VIEWPORT (viewport), box);
  adw_clamp_scrollable_set_child (ADW_CLAMP_SCROLLABLE (clamp), viewport);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), clamp);

  /* Floating "back to top" button: revealed once scrolled away from top */
  {
    GtkWidget    *overlay;
    GtkRevealer  *revealer;
    GtkWidget    *btn;
    GtkAdjustment *vadj;

    overlay = gtk_overlay_new ();
    gtk_overlay_set_child (GTK_OVERLAY (overlay), scroll);

    revealer = GTK_REVEALER (gtk_revealer_new ());
    gtk_revealer_set_transition_type (revealer, GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
    gtk_revealer_set_reveal_child (revealer, FALSE);
    gtk_widget_set_halign (GTK_WIDGET (revealer), GTK_ALIGN_END);
    gtk_widget_set_valign (GTK_WIDGET (revealer), GTK_ALIGN_END);
    gtk_widget_set_margin_end (GTK_WIDGET (revealer), 24);
    gtk_widget_set_margin_bottom (GTK_WIDGET (revealer), 24);

    btn = gtk_button_new_from_icon_name ("go-top-symbolic");
    gtk_widget_add_css_class (btn, "circular");
    gtk_widget_add_css_class (btn, "suggested-action");
    gtk_accessible_update_property (GTK_ACCESSIBLE (btn),
                                    GTK_ACCESSIBLE_PROPERTY_LABEL, "Back to top", -1);
    gtk_widget_set_tooltip_text (btn, "Back to top");
    g_signal_connect_swapped (btn, "clicked", G_CALLBACK (to_top_clicked_cb), self);

    gtk_revealer_set_child (revealer, btn);
    gtk_overlay_add_overlay (GTK_OVERLAY (overlay), GTK_WIDGET (revealer));

    self->to_top_revealer = revealer;

    vadj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->scroll));
    g_signal_connect_swapped (vadj, "notify::value",
                              G_CALLBACK (on_scroll_changed_cb), self);

    adw_bin_set_child (ADW_BIN (self), overlay);
  }

  setup_all_groups (self);

  /* Ensure every app in the store has a core label (default: "New") */
  {
    GListModel *all_groups = bz_state_info_get_all_entry_groups (self->state);
    GPtrArray  *all_ids    = g_ptr_array_new ();

    if (all_groups != NULL)
      {
        guint n_all = g_list_model_get_n_items (all_groups);

        for (guint i = 0; i < n_all; i++)
          {
            g_autoptr (GObject) obj = g_list_model_get_item (all_groups, i);

            if (obj != NULL && BZ_IS_ENTRY_GROUP (obj))
              {
                const char *id = bz_entry_group_get_id (BZ_ENTRY_GROUP (obj));
                if (id != NULL)
                  g_ptr_array_add (all_ids, (gpointer) id);
              }
          }

        trace (
            "populate_categories: all_entry_groups has %u items, extracted %u IDs",
            n_all, all_ids->len);
        if (all_ids->len > 0)
          cz_custom_label_store_ensure_app_ids (
              self->label_store,
              (const char * const *) all_ids->pdata,
              all_ids->len);

        g_ptr_array_unref (all_ids);
      }
  }

  /* If page is already mapped (visible tab) when UI finishes building
   * (e.g., categories arrived late while page was already visible),
   * trigger trending click. */
  if (gtk_widget_get_mapped (GTK_WIDGET (self)))
    {
      GtkWidget *ch;

      trace ("populate_categories: page already mapped, clicking trending");
      trigger_trending_click (self);
      /* Also auto-select "New" core pill (monoselect) */
      ch = gtk_widget_get_first_child (self->custom_pill_box);
      while (ch != NULL)
        {
          if (GTK_IS_BUTTON (ch) &&
              g_strcmp0 (gtk_button_get_label (GTK_BUTTON (ch)), "New") == 0)
            {
              GtkWidget *sib;

              if (!gtk_widget_has_css_class (ch, "selected"))
                {
                  for (sib = gtk_widget_get_first_child (self->custom_pill_box);
                       sib != NULL; sib = gtk_widget_get_next_sibling (sib))
                    if (GTK_IS_BUTTON (sib) && sib != ch)
                      gtk_widget_remove_css_class (sib, "selected");

                  gtk_widget_add_css_class (ch, "selected");
                  self->selected_core_tile = ch;
                }
              break;
            }
          ch = gtk_widget_get_next_sibling (ch);
        }
    }
}

static void
on_flathub_loaded (CzCustomPage *self,
                   GParamSpec   *pspec,
                   BzStateInfo  *state)
{
  BzFlathubState *flathub = bz_state_info_get_flathub (state);
  if (flathub != NULL)
    populate_categories (self);
}

static const char *cz_custom_css =
  ".search-pill {"
  "  background-color: rgba(96, 165, 250, 0.20);"
  "  color: @theme_fg_color;"
  "}"
  ".search-pill.selected {"
  "  background-color: rgba(96, 165, 250, 0.55);"
  "  color: white;"
  "}"
  ".custom-pill {"
  "  background-color: rgba(167, 139, 250, 0.20);"
  "  color: @theme_fg_color;"
  "}"
  ".custom-pill.selected {"
  "  background-color: rgba(167, 139, 250, 0.55);"
  "  color: @theme_fg_color;"
  "}"
  ".noncore-pill {"
  "  background-color: rgba(45, 212, 191, 0.20);"
  "  color: @theme_fg_color;"
  "}"
  ".noncore-pill.selected {"
  "  background-color: rgba(45, 212, 191, 0.55);"
  "  color: @theme_fg_color;"
  "}";

/* ---- Delayed auto-save on label-store changes ---- */

static void
on_label_store_changed (CzCustomLabelStore *store,
                        CzCustomPage       *self)
{
  /* Rebuild noncore pill UI in real time */
  cz_custom_page_rebuild_noncore_pills (self);
}

static void
cz_custom_page_constructed (GObject *object)
{
  CzCustomPage    *self = CZ_CUSTOM_PAGE (object);
  BzFlathubState  *flathub = NULL;
  GtkCssProvider  *css = NULL;
  GdkDisplay      *display = NULL;

  G_OBJECT_CLASS (cz_custom_page_parent_class)->constructed (object);

  css = gtk_css_provider_new ();
  gtk_css_provider_load_from_string (css, cz_custom_css);
  display = gdk_display_get_default ();
  if (display != NULL)
    gtk_style_context_add_provider_for_display (display,
        GTK_STYLE_PROVIDER (css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  self->state = bz_state_info_get_default ();
  if (self->state == NULL)
    return;

  /* Create label store and try loading from disk */
  {
    const char *data_dir = g_get_user_data_dir ();
    const char *app_id   = "io.github.kolunmi.Bazaar";

    if (data_dir != NULL)
      self->label_store_path = g_build_filename (
          data_dir, app_id, "custom-labels.db", NULL);
  }

  self->label_store = cz_custom_label_store_new ();
  if (self->label_store_path != NULL)
    {
      g_autoptr (GError) err = NULL;
      if (!cz_custom_label_store_load_from_path (self->label_store,
                                                 self->label_store_path))
        {
          g_warning ("Failed to load custom label store from %s",
                     self->label_store_path);
        }
    }
  g_signal_connect (self->label_store, "changed",
                    G_CALLBACK (on_label_store_changed), self);

  {
    GListModel *all_groups = bz_state_info_get_all_entry_groups (self->state);
    trace ("cz_custom_page_constructed: all_groups=%p, connecting notify handler=%d",
           all_groups, all_groups == NULL);
    if (all_groups == NULL)
      g_signal_connect_swapped (self->state, "notify::all-entry-groups",
                                G_CALLBACK (on_all_entry_groups_loaded), self);
  }

  flathub = bz_state_info_get_flathub (self->state);
  if (flathub != NULL)
    {
      populate_categories (self);
    }
  else
    {
      g_signal_connect_swapped (self->state, "notify::flathub",
                                G_CALLBACK (on_flathub_loaded), self);
    }
  /* NOTE: setup_all_groups is called at the end of populate_categories,
   * AFTER the UI widgets (pill_box, apps_list_view) are created */

  /* Auto-select trending every time this page becomes the visible tab.
   * AdwViewStack uses gtk_widget_set_child_visible() internally (like GtkStack),
   * which fires the ::map signal when the page transitions from unmapped→mapped,
   * i.e. when it becomes the visible child of a mapped stack. */
  trace ("cz_custom_page_constructed: connecting ::map");
  g_signal_connect (self, "map",
                    G_CALLBACK (on_map), NULL);
}

void
cz_custom_page_rebuild_noncore_pills (CzCustomPage *self)
{
  GPtrArray *noncore_names;

  g_return_if_fail (CZ_IS_CUSTOM_PAGE (self));

  if (self->noncore_pill_box == NULL)
    return;

  /* Remove all existing pills */
  {
    GtkWidget *child;

    while ((child = gtk_widget_get_first_child (self->noncore_pill_box)) != NULL)
      gtk_box_remove (GTK_BOX (self->noncore_pill_box), child);
  }

  noncore_names = cz_custom_label_store_get_all_noncore_label_names (
      self->label_store);

  if (noncore_names->len == 0)
    {
      GtkWidget *pill = gtk_button_new_with_label ("Empty");
      gtk_widget_add_css_class (pill, "small-pill");
      gtk_widget_add_css_class (pill, "noncore-pill");
      gtk_widget_set_sensitive (pill, FALSE);
      gtk_box_append (GTK_BOX (self->noncore_pill_box), pill);
    }
  else
    {
      for (guint j = 0; j < noncore_names->len; j++)
        {
          const char *name = (const char *) g_ptr_array_index (noncore_names, j);

          /* Defensive: skip NULL or empty names */
          if (name == NULL || *name == '\0')
            continue;

          GtkWidget *pill = gtk_button_new_with_label (name);

          /* Belt-and-suspenders: explicitly set label text */
          gtk_button_set_label (GTK_BUTTON (pill), name);

          gtk_widget_add_css_class (pill, "small-pill");
          gtk_widget_add_css_class (pill, "noncore-pill");
          g_signal_connect_swapped (pill, "clicked",
                                    G_CALLBACK (noncore_label_selected), self);
          gtk_box_append (GTK_BOX (self->noncore_pill_box), pill);
        }
    }

  g_ptr_array_unref (noncore_names);
}

static void
cz_custom_page_finalize (GObject *object)
{
  CzCustomPage *self = CZ_CUSTOM_PAGE (object);

  trace ("cz_custom_page_finalize called");

  if (self->state != NULL)
    {
      BzFlathubState *flathub = bz_state_info_get_flathub (self->state);

      g_signal_handlers_disconnect_by_func (self->state, on_flathub_loaded, self);
      g_signal_handlers_disconnect_by_func (self->state, on_all_entry_groups_loaded, self);
      if (flathub != NULL)
        g_signal_handlers_disconnect_by_func (flathub, on_categories_loaded, self);
    }

  if (self->all_groups_changed_id != 0 && self->all_groups != NULL)
    g_signal_handler_disconnect (self->all_groups, self->all_groups_changed_id);

  g_signal_handlers_disconnect_by_func (self, on_map, NULL);

  clear_filter_snapshot (self);

  /* Cancel any pending delayed auto-save — auto-save on ::changed is
   * the sole persistence mechanism; finalize never writes to disk so
   * that tests (which create and destroy pages) won't clobber the
   * real label DB. */
  if (self->label_store != NULL)
    {
      guint id;

      id = GPOINTER_TO_UINT (g_object_get_data (
          G_OBJECT (self->label_store), "cz-delayed-save-id"));
      if (id != 0)
        {
          g_source_remove (id);
          g_object_set_data (G_OBJECT (self->label_store),
                             "cz-delayed-save-id", NULL);
        }

      g_clear_object (&self->label_store);
    }
  g_free (self->label_store_path);

  G_OBJECT_CLASS (cz_custom_page_parent_class)->finalize (object);
}

static void
cz_custom_page_class_init (CzCustomPageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = cz_custom_page_constructed;
  object_class->finalize = cz_custom_page_finalize;

  g_type_ensure (BZ_TYPE_DYNAMIC_LIST_VIEW);
  g_type_ensure (BZ_TYPE_APP_TILE);
  g_type_ensure (BZ_TYPE_ENTRY_GROUP);
  g_type_ensure (CZ_TYPE_CUSTOM_LABEL_STORE);
}

static void
cz_custom_page_init (CzCustomPage *self)
{
}

CzCustomPage *
cz_custom_page_new (void)
{
  return g_object_new (CZ_TYPE_CUSTOM_PAGE, NULL);
}

CzCustomLabelStore *
cz_custom_page_get_label_store (CzCustomPage *self)
{
  g_return_val_if_fail (CZ_IS_CUSTOM_PAGE (self), NULL);
  return self->label_store;
}
