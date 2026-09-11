/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * BzLabelStore — SQLite-backed label persistence.
 *
 * Replaces the JSON file (custom-labels.json) used by both the main
 * app and the custom companion app.  Every mutation commits its own
 * row-level transaction (BEGIN IMMEDIATE), which removes the
 * lost-update race that the whole-file JSON writes suffered from.
 *
 * A timestamped backup (.db, via sqlite3_backup_init) is written into
 * <backup_dir>/backups/ after every transaction that changed data and
 * pruned to BZ_LABEL_STORE_BACKUP_RETENTION files.  If an external
 * backup dir is configured (BZ_PROJECT_BACKUP_DIR env var, or the
 * compile-time BZ_PROJECT_BACKUP_DIR_DEFAULT) a copy is placed there
 * as well.
 *
 * On open the primary DB is checked with PRAGMA integrity_check.  If
 * it fails the newest valid backup is restored; failing that, a legacy
 * custom-labels.json next to the DB is migrated in (the legacy file is
 * left untouched).
 */

#ifndef BZ_LABEL_STORE_H
#define BZ_LABEL_STORE_H

#include <glib.h>

G_BEGIN_DECLS

#define BZ_LABEL_STORE_ERROR (bz_label_store_error_quark ())

GQuark bz_label_store_error_quark (void);

typedef struct _BzLabelStore BzLabelStore;

BzLabelStore *bz_label_store_open (const char *db_path,
                                   const char *backup_dir,
                                   GError    **error);

void
bz_label_store_close (BzLabelStore *store);

char *
bz_label_store_get_core_label (BzLabelStore *store,
                               const char   *app_id);

gboolean
bz_label_store_set_core_label (BzLabelStore *store,
                               const char   *app_id,
                               const char   *label,
                               GError      **error);

gboolean
bz_label_store_has_noncore_label (BzLabelStore *store,
                                  const char   *app_id,
                                  const char   *label);

gboolean
bz_label_store_add_noncore_label (BzLabelStore *store,
                                  const char   *app_id,
                                  const char   *label,
                                  GError      **error);

gboolean
bz_label_store_remove_noncore_label (BzLabelStore *store,
                                     const char   *app_id,
                                     const char   *label,
                                     GError      **error);

gboolean
bz_label_store_add_label_name (BzLabelStore *store,
                               const char   *label,
                               GError      **error);

gboolean
bz_label_store_remove_label_name (BzLabelStore *store,
                                  const char   *label,
                                  GError      **error);

gboolean
bz_label_store_rename_label_name (BzLabelStore *store,
                                  const char   *old_label,
                                  const char   *new_label,
                                  GError      **error);

char **
bz_label_store_get_all_label_names (BzLabelStore *store);

char **
bz_label_store_get_noncore_labels (BzLabelStore *store,
                                   const char   *app_id);

char **
bz_label_store_get_noncore_app_ids (BzLabelStore *store);

char **
bz_label_store_get_core_app_ids (BzLabelStore *store);

gboolean
bz_label_store_ensure_app_ids (BzLabelStore      *store,
                               const char *const *app_ids,
                               gsize              n_app_ids,
                               GError           **error);

guint
bz_label_store_count_noncore_label (BzLabelStore *store,
                                    const char   *label);

gboolean
bz_label_store_export (BzLabelStore *store,
                       GString      *out,
                       GError      **error);

gboolean
bz_label_store_verify_integrity (BzLabelStore *store,
                                 GError      **error);

G_END_DECLS

#endif /* BZ_LABEL_STORE_H */
