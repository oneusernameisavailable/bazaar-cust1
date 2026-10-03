#pragma once

#include <glib.h>
#include <gio/gio.h>
#include "bz-flathub-category.h"

G_BEGIN_DECLS

/* Sentinel text for the always-present default row/pill of every category's
 * custom-label selector: selecting it means "no custom label in this
 * category" (the label is never persisted — it is the absence of a row). */
#define CZ_UNLABELED_PILL_TEXT "Unlabeled"

gboolean   cz_category_get_show_in_list (BzFlathubCategory *category);
const char *cz_category_get_short_name   (BzFlathubCategory *category);
GHashTable *cz_category_build_id_set     (BzFlathubCategory *category);
gboolean   cz_category_is_appstream      (BzFlathubCategory *category);
gboolean   cz_category_app_filter        (gpointer item, gpointer user_data);

/*
 * Computes the members of the virtual "Uncategorized" tab: apps whose
 * appstream category bitmask is zero and that appear in no collection's
 * applications list (Trending, Mobile, Adwaita, Popular, Recently Added,
 * Updated, game-only, emulators, launchers, game-tools, KDE, ...).  The
 * union of "claimed" ids is taken from the categories model itself, so any
 * collection the app tracks keeps its members out of here.
 *
 * Returns a NULL-terminated GStrv of app ids (transfer full) or NULL when
 * either input is NULL.
 */
char **cz_category_build_uncategorized (GListModel *categories,
                                        GListModel *all_groups);

/*
 * Computes the members of the virtual "Addons" tab: addon/extension groups
 * that are not EOL.  EOL addons stay out (frozen, hidden everywhere else);
 * parent apps never carry addons as appstream categories, so nothing else
 * claims them.
 *
 * Returns a NULL-terminated GStrv of group ids (transfer full) or NULL when
 * all_groups is NULL.
 */
char **cz_category_build_addons (GListModel *all_groups);

/*
 * Deduplicates the browsed category tabs: each app is assigned a single
 * primary tab.  Appstream (metadata) categories win; Trending/Mobile/Adwaita
 * only claim apps with no appstream category.  Among candidates, the
 * smallest tab (rarity) wins with a deterministic table-order tie-break.
 *
 * The engine carries no persistent snapshot: it re-derives membership from
 * the live model whenever invalidated, so newly-added apps are picked up by
 * the next query without staleness.
 */
typedef struct _CzCategoryPrimary CzCategoryPrimary;

CzCategoryPrimary *cz_category_primary_new          (GListModel *categories);
CzCategoryPrimary *cz_category_primary_ref          (CzCategoryPrimary *self);
void               cz_category_primary_unref        (CzCategoryPrimary *self);
void               cz_category_primary_invalidate   (CzCategoryPrimary *self);
GHashTable        *cz_category_primary_build_id_set (CzCategoryPrimary *self,
                                                      BzFlathubCategory *category);

/*
 * Resolves the primary visible tab that owns the given app id, using the
 * same rule-A dedup as build_id_set (metadata tab wins, else smallest tab,
 * ties break to cz_category_info[] order).  Returns the owning tab's name,
 * which is owned by the engine and valid until the engine is unreferenced,
 * or NULL when no visible tab claims the app.
 */
const char *cz_category_primary_resolve_tab (CzCategoryPrimary *self,
                                             const char        *app_id);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (CzCategoryPrimary, cz_category_primary_unref)

G_END_DECLS
