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

gboolean   cz_custom_label_store_has_noncore_label   (CzCustomLabelStore *self,
                                                       const char         *app_id,
                                                       const char         *label);
void       cz_custom_label_store_add_noncore_label   (CzCustomLabelStore *self,
                                                       const char         *app_id,
                                                       const char         *label);
void       cz_custom_label_store_remove_noncore_label (CzCustomLabelStore *self,
                                                        const char         *app_id,
                                                        const char         *label);

void      cz_custom_label_store_ensure_app_ids (CzCustomLabelStore *self,
                                                 const char * const *app_ids,
                                                 guint               n_ids);

GPtrArray *cz_custom_label_store_get_all_noncore_label_names (
    CzCustomLabelStore *self);

gboolean   cz_custom_label_store_add_noncore_label_name (
    CzCustomLabelStore *self,
    const char         *name);

gboolean   cz_custom_label_store_remove_noncore_label_name (
    CzCustomLabelStore *self,
    const char         *name);

G_END_DECLS
