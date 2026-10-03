#pragma once

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define CZ_TYPE_CUSTOM_LABEL_STORE (cz_custom_label_store_get_type ())
G_DECLARE_FINAL_TYPE (CzCustomLabelStore, cz_custom_label_store, CZ, CUSTOM_LABEL_STORE, GObject)

CzCustomLabelStore *cz_custom_label_store_new (void);

gboolean  cz_custom_label_store_load_from_path (CzCustomLabelStore *self,
                                                 const char         *path);
gboolean  cz_custom_label_store_save_to_path   (CzCustomLabelStore *self,
                                                 const char         *path);

const char *cz_custom_label_store_get_core_label    (CzCustomLabelStore *self,
                                                      const char         *app_id);
void        cz_custom_label_store_set_core_label    (CzCustomLabelStore *self,
                                                      const char         *app_id,
                                                      const char         *label);

void       cz_custom_label_store_ensure_app_ids (CzCustomLabelStore *self,
                                                  const char * const *app_ids,
                                                  guint               n_ids);

/* Per-category mono custom-label API.  Each app carries at most one
 * custom label inside its category; "Unlabeled" is the absence of an
 * assignment and is represented by NULL/empty label. */
const char *cz_custom_label_store_get_app_custom_label (
    CzCustomLabelStore *self,
    const char         *app_id);

const char *cz_custom_label_store_get_app_custom_category (
    CzCustomLabelStore *self,
    const char         *app_id);

void cz_custom_label_store_set_app_custom_label (
    CzCustomLabelStore *self,
    const char         *app_id,
    const char         *category,
    const char         *label);

GPtrArray *cz_custom_label_store_get_category_label_names (
    CzCustomLabelStore *self,
    const char         *category);

gboolean cz_custom_label_store_add_category_label_name (
    CzCustomLabelStore *self,
    const char         *category,
    const char         *name);

gboolean cz_custom_label_store_remove_category_label_name (
    CzCustomLabelStore *self,
    const char         *category,
    const char         *name);

guint cz_custom_label_store_count_category_label_assignments (
    CzCustomLabelStore *self,
    const char         *category,
    const char         *name);

G_END_DECLS
