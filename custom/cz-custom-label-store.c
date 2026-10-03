#include "config.h"

#include "bz-label-store.h"
#include "cz-custom-label-store.h"

#include <glib/gstdio.h>

#define DEFAULT_CORE_LABEL "New"

enum
{
  CHANGED_SIGNAL,
  N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

typedef struct
{
  gchar *category;
  gchar *label;
} CzCustomLabelAssignment;

static void
custom_label_assignment_free (gpointer data)
{
  CzCustomLabelAssignment *a = data;

  g_free (a->category);
  g_free (a->label);
  g_free (a);
}

struct _CzCustomLabelStore
{
  GObject       parent_instance;
  GHashTable   *core_map;             /* app_id → g_strdup'd label string */
  GHashTable   *noncore_map;          /* app_id → GHashTable<label, NULL> */
  GHashTable   *global_noncore_names; /* label → NULL (set of all known noncore label names) */
  GHashTable   *custom_label_map;     /* app_id → CzCustomLabelAssignment* */
  GHashTable   *category_label_names; /* category → GHashTable<name, NULL> */
  BzLabelStore *store;                /* open SQLite store backing this facade, or NULL */
};

G_DEFINE_FINAL_TYPE (CzCustomLabelStore, cz_custom_label_store, G_TYPE_OBJECT)

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static GHashTable *
make_label_set (void)
{
  return g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static gboolean
label_set_contains (GHashTable *set, const char *label)
{
  return label != NULL && g_hash_table_contains (set, label);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

static void
hydrate_from_store (CzCustomLabelStore *self)
{
  char **app_ids;
  char **names;
  guint  i;

  g_hash_table_remove_all (self->core_map);
  g_hash_table_remove_all (self->noncore_map);
  g_hash_table_remove_all (self->global_noncore_names);
  g_hash_table_remove_all (self->custom_label_map);
  g_hash_table_remove_all (self->category_label_names);

  app_ids = bz_label_store_get_core_app_ids (self->store);
  for (i = 0; app_ids != NULL && app_ids[i] != NULL; i++)
    g_hash_table_insert (self->core_map,
                         g_strdup (app_ids[i]),
                         bz_label_store_get_core_label (self->store, app_ids[i]));
  g_strfreev (app_ids);

  app_ids = bz_label_store_get_noncore_app_ids (self->store);
  for (i = 0; app_ids != NULL && app_ids[i] != NULL; i++)
    {
      char      **labels = bz_label_store_get_noncore_labels (self->store, app_ids[i]);
      GHashTable *set    = make_label_set ();
      guint       j;

      for (j = 0; labels != NULL && labels[j] != NULL; j++)
        {
          g_hash_table_add (set, g_strdup (labels[j]));
          if (!g_hash_table_contains (self->global_noncore_names, labels[j]))
            g_hash_table_add (self->global_noncore_names, g_strdup (labels[j]));
        }
      g_strfreev (labels);
      g_hash_table_insert (self->noncore_map, g_strdup (app_ids[i]), set);
    }
  g_strfreev (app_ids);

  names = bz_label_store_get_all_label_names (self->store);
  for (i = 0; names != NULL && names[i] != NULL; i++)
    {
      if (!g_hash_table_contains (self->global_noncore_names, names[i]))
        g_hash_table_add (self->global_noncore_names, g_strdup (names[i]));
    }
  g_strfreev (names);

  app_ids = bz_label_store_get_app_custom_label_app_ids (self->store);
  for (i = 0; app_ids != NULL && app_ids[i] != NULL; i++)
    {
      CzCustomLabelAssignment *assignment;
      char                    *category = NULL;
      char                    *label    = NULL;

      if (!bz_label_store_get_app_custom_label (self->store, app_ids[i],
                                                &category, &label))
        continue;

      assignment     = g_new0 (CzCustomLabelAssignment, 1);
      assignment->category = category;
      assignment->label    = label;
      g_hash_table_insert (self->custom_label_map,
                           g_strdup (app_ids[i]), assignment);
    }
  g_strfreev (app_ids);

  names = bz_label_store_get_categories_with_label_names (self->store);
  for (i = 0; names != NULL && names[i] != NULL; i++)
    {
      char      **cat_names = bz_label_store_get_category_label_names (self->store,
                                                                       names[i]);
      GHashTable *set       = make_label_set ();
      guint       j;

      for (j = 0; cat_names != NULL && cat_names[j] != NULL; j++)
        g_hash_table_add (set, g_strdup (cat_names[j]));
      g_strfreev (cat_names);
      g_hash_table_insert (self->category_label_names,
                           g_strdup (names[i]), set);
    }
  g_strfreev (names);
}

gboolean
cz_custom_label_store_load_from_path (CzCustomLabelStore *self,
                                      const char         *path)
{
  BzLabelStore *new_store;
  char         *dir;

  g_return_val_if_fail (CZ_IS_CUSTOM_LABEL_STORE (self), FALSE);
  g_return_val_if_fail (path != NULL, FALSE);

  dir       = g_path_get_dirname (path);
  new_store = bz_label_store_open (path, dir, NULL);
  g_free (dir);
  if (new_store == NULL)
    {
      g_warning ("Failed to open label store at %s", path);
      return FALSE;
    }

  if (self->store != NULL)
    bz_label_store_close (self->store);
  self->store = new_store;

  hydrate_from_store (self);
  return TRUE;
}

gboolean
cz_custom_label_store_save_to_path (CzCustomLabelStore *self,
                                    const char         *path)
{
  BzLabelStore  *tmp;
  GHashTableIter map_iter;
  gpointer       key, value;
  char          *dir;
  gboolean       ok;

  g_return_val_if_fail (CZ_IS_CUSTOM_LABEL_STORE (self), FALSE);
  g_return_val_if_fail (path != NULL, FALSE);

  dir = g_path_get_dirname (path);
  g_mkdir_with_parents (dir, 0755);
  tmp = bz_label_store_open (path, dir, NULL);
  g_free (dir);
  if (tmp == NULL)
    return FALSE;

  ok = TRUE;

  g_hash_table_iter_init (&map_iter, self->core_map);
  while (g_hash_table_iter_next (&map_iter, &key, &value) && ok)
    ok = bz_label_store_set_core_label (tmp, (const char *) key,
                                        (const char *) value, NULL);

  if (ok)
    {
      g_hash_table_iter_init (&map_iter, self->noncore_map);
      while (g_hash_table_iter_next (&map_iter, &key, &value) && ok)
        {
          GHashTable    *set = (GHashTable *) value;
          GHashTableIter set_iter;
          gpointer       label_key;

          g_hash_table_iter_init (&set_iter, set);
          while (g_hash_table_iter_next (&set_iter, &label_key, NULL) && ok)
            ok = bz_label_store_add_noncore_label (tmp, (const char *) key,
                                                   (const char *) label_key, NULL);
        }
    }

  if (ok)
    {
      g_hash_table_iter_init (&map_iter, self->global_noncore_names);
      while (g_hash_table_iter_next (&map_iter, &key, NULL) && ok)
        ok = bz_label_store_add_label_name (tmp, (const char *) key, NULL);
    }

  if (ok)
    {
      GHashTableIter assignment_iter;
      gpointer       app_key;
      CzCustomLabelAssignment *assignment;

      g_hash_table_iter_init (&assignment_iter, self->custom_label_map);
      while (g_hash_table_iter_next (&assignment_iter, &app_key,
                                     (gpointer *) &assignment) && ok)
        {
          if (assignment != NULL && assignment->label != NULL)
            ok = bz_label_store_set_app_custom_label (tmp,
                                                      (const char *) app_key,
                                                      assignment->category,
                                                      assignment->label,
                                                      NULL);
        }
    }

  if (ok)
    {
      GHashTable *entry;
      GHashTableIter set_iter;
      gpointer       cat_key;
      gpointer       name_key;
      gpointer       stub;

      g_hash_table_iter_init (&map_iter, self->category_label_names);
      while (g_hash_table_iter_next (&map_iter, &cat_key,
                                     (gpointer *) &entry) && ok)
        {
          g_hash_table_iter_init (&set_iter, entry);
          while (g_hash_table_iter_next (&set_iter, &name_key, &stub) && ok)
            ok = bz_label_store_add_category_label_name (tmp,
                                                         (const char *) cat_key,
                                                         (const char *) name_key,
                                                         NULL);
        }
    }

  bz_label_store_close (tmp);
  return ok;
}

const char *
cz_custom_label_store_get_core_label (CzCustomLabelStore *self,
                                      const char         *app_id)
{
  const char *label;

  g_return_val_if_fail (CZ_IS_CUSTOM_LABEL_STORE (self), DEFAULT_CORE_LABEL);
  g_return_val_if_fail (app_id != NULL, DEFAULT_CORE_LABEL);

  label = (const char *) g_hash_table_lookup (self->core_map, app_id);
  return label != NULL ? label : DEFAULT_CORE_LABEL;
}

void
cz_custom_label_store_set_core_label (CzCustomLabelStore *self,
                                      const char         *app_id,
                                      const char         *label)
{
  g_return_if_fail (CZ_IS_CUSTOM_LABEL_STORE (self));
  g_return_if_fail (app_id != NULL);
  g_return_if_fail (label != NULL);

  g_hash_table_insert (self->core_map, g_strdup (app_id), g_strdup (label));
  if (self->store != NULL)
    bz_label_store_set_core_label (self->store, app_id, label, NULL);
  g_signal_emit (self, signals[CHANGED_SIGNAL], 0);
}


void
cz_custom_label_store_ensure_app_ids (CzCustomLabelStore *self,
                                      const char *const  *app_ids,
                                      guint               n_ids)
{
  guint i;

  g_return_if_fail (CZ_IS_CUSTOM_LABEL_STORE (self));
  g_return_if_fail (app_ids != NULL || n_ids == 0);

  for (i = 0; i < n_ids; i++)
    {
      const char *app_id = app_ids[i];
      if (app_id == NULL)
        continue;

      if (!g_hash_table_contains (self->core_map, app_id))
        g_hash_table_insert (self->core_map,
                             g_strdup (app_id),
                             g_strdup (DEFAULT_CORE_LABEL));
    }

  if (self->store != NULL)
    bz_label_store_ensure_app_ids (self->store, app_ids, n_ids, NULL);
}

/* ------------------------------------------------------------------ */
/*  Per-category mono custom-label API                                  */
/* ------------------------------------------------------------------ */

const char *
cz_custom_label_store_get_app_custom_label (CzCustomLabelStore *self,
                                            const char         *app_id)
{
  CzCustomLabelAssignment *assignment;

  g_return_val_if_fail (CZ_IS_CUSTOM_LABEL_STORE (self), NULL);
  g_return_val_if_fail (app_id != NULL, NULL);

  assignment = (CzCustomLabelAssignment *)
      g_hash_table_lookup (self->custom_label_map, app_id);
  if (assignment == NULL || assignment->label == NULL)
    return NULL;

  return assignment->label;
}

const char *
cz_custom_label_store_get_app_custom_category (CzCustomLabelStore *self,
                                               const char         *app_id)
{
  CzCustomLabelAssignment *assignment;

  g_return_val_if_fail (CZ_IS_CUSTOM_LABEL_STORE (self), NULL);
  g_return_val_if_fail (app_id != NULL, NULL);

  assignment = (CzCustomLabelAssignment *)
      g_hash_table_lookup (self->custom_label_map, app_id);
  if (assignment == NULL)
    return NULL;

  return assignment->category;
}

void
cz_custom_label_store_set_app_custom_label (CzCustomLabelStore *self,
                                            const char         *app_id,
                                            const char         *category,
                                            const char         *label)
{
  CzCustomLabelAssignment *assignment;

  g_return_if_fail (CZ_IS_CUSTOM_LABEL_STORE (self));
  g_return_if_fail (app_id != NULL);
  g_return_if_fail (category != NULL && *category != '\0');

  assignment = (CzCustomLabelAssignment *)
      g_hash_table_lookup (self->custom_label_map, app_id);

  /* NULL/empty label means "Unlabeled" — clear the assignment. */
  if (label == NULL || *label == '\0')
    {
      if (assignment == NULL)
        return;
      g_hash_table_remove (self->custom_label_map, app_id);
      if (self->store != NULL)
        bz_label_store_set_app_custom_label (self->store, app_id,
                                             category, NULL, NULL);
      g_signal_emit (self, signals[CHANGED_SIGNAL], 0);
      return;
    }

  if (assignment == NULL)
    {
      assignment        = g_new0 (CzCustomLabelAssignment, 1);
      assignment->category = g_strdup (category);
      assignment->label    = g_strdup (label);
      g_hash_table_insert (self->custom_label_map, g_strdup (app_id),
                           assignment);
    }
  else
    {
      g_free (assignment->category);
      g_free (assignment->label);
      assignment->category = g_strdup (category);
      assignment->label    = g_strdup (label);
    }

  if (self->store != NULL)
    bz_label_store_set_app_custom_label (self->store, app_id,
                                         category, label, NULL);
  g_signal_emit (self, signals[CHANGED_SIGNAL], 0);
}

GPtrArray *
cz_custom_label_store_get_category_label_names (CzCustomLabelStore *self,
                                                const char         *category)
{
  GHashTable *set;
  GHashTableIter iter;
  gpointer       k;
  GPtrArray     *names;

  g_return_val_if_fail (CZ_IS_CUSTOM_LABEL_STORE (self), NULL);
  g_return_val_if_fail (category != NULL, NULL);

  names = g_ptr_array_new_with_free_func (g_free);
  set   = (GHashTable *) g_hash_table_lookup (self->category_label_names,
                                              category);
  if (set == NULL)
    return names;

  g_hash_table_iter_init (&iter, set);
  while (g_hash_table_iter_next (&iter, &k, NULL))
    g_ptr_array_add (names, g_strdup ((const char *) k));

  return names;
}

gboolean
cz_custom_label_store_add_category_label_name (CzCustomLabelStore *self,
                                               const char         *category,
                                               const char         *name)
{
  GHashTable *set;

  g_return_val_if_fail (CZ_IS_CUSTOM_LABEL_STORE (self), FALSE);
  g_return_val_if_fail (category != NULL && *category != '\0', FALSE);
  g_return_val_if_fail (name != NULL && *name != '\0', FALSE);

  set = (GHashTable *) g_hash_table_lookup (self->category_label_names,
                                            category);
  if (set == NULL)
    {
      set = make_label_set ();
      g_hash_table_insert (self->category_label_names,
                           g_strdup (category), set);
    }

  if (g_hash_table_contains (set, name))
    return FALSE;

  g_hash_table_add (set, g_strdup (name));
  if (self->store != NULL)
    bz_label_store_add_category_label_name (self->store, category, name, NULL);
  g_signal_emit (self, signals[CHANGED_SIGNAL], 0);
  return TRUE;
}

gboolean
cz_custom_label_store_remove_category_label_name (CzCustomLabelStore *self,
                                                  const char         *category,
                                                  const char         *name)
{
  GHashTable   *set;
  GHashTableIter map_iter;
  gpointer       app_key, assignment_ptr;
  gboolean       was_present;

  g_return_val_if_fail (CZ_IS_CUSTOM_LABEL_STORE (self), FALSE);
  g_return_val_if_fail (category != NULL && *category != '\0', FALSE);
  g_return_val_if_fail (name != NULL && *name != '\0', FALSE);

  set = (GHashTable *) g_hash_table_lookup (self->category_label_names,
                                            category);
  if (set == NULL)
    return FALSE;

  was_present = g_hash_table_remove (set, name);
  if (g_hash_table_size (set) == 0)
    g_hash_table_remove (self->category_label_names, category);

  /* Cascade: drop assignments of this label within this category. */
  g_hash_table_iter_init (&map_iter, self->custom_label_map);
  while (g_hash_table_iter_next (&map_iter, &app_key, &assignment_ptr))
    {
      CzCustomLabelAssignment *assignment = assignment_ptr;
      if (assignment != NULL &&
          g_strcmp0 (assignment->category, category) == 0 &&
          g_strcmp0 (assignment->label, name) == 0)
        g_hash_table_iter_remove (&map_iter);
    }

  if (self->store != NULL)
    bz_label_store_remove_category_label_name (self->store, category, name,
                                               NULL);

  g_signal_emit (self, signals[CHANGED_SIGNAL], 0);
  return was_present;
}

guint
cz_custom_label_store_count_category_label_assignments (CzCustomLabelStore *self,
                                                        const char         *category,
                                                        const char         *name)
{
  GHashTableIter map_iter;
  gpointer       app_key, assignment_ptr;
  guint          count = 0;

  g_return_val_if_fail (CZ_IS_CUSTOM_LABEL_STORE (self), 0);
  g_return_val_if_fail (category != NULL, 0);
  g_return_val_if_fail (name != NULL, 0);

  g_hash_table_iter_init (&map_iter, self->custom_label_map);
  while (g_hash_table_iter_next (&map_iter, &app_key, &assignment_ptr))
    {
      CzCustomLabelAssignment *assignment = assignment_ptr;
      if (assignment != NULL &&
          g_strcmp0 (assignment->category, category) == 0 &&
          g_strcmp0 (assignment->label, name) == 0)
        count++;
    }

  return count;
}

/* ------------------------------------------------------------------ */
/*  GObject                                                             */
/* ------------------------------------------------------------------ */

static void
cz_custom_label_store_finalize (GObject *object)
{
  CzCustomLabelStore *self = CZ_CUSTOM_LABEL_STORE (object);

  if (self->store != NULL)
    bz_label_store_close (self->store);

  g_hash_table_unref (self->core_map);
  g_hash_table_unref (self->noncore_map);
  g_hash_table_unref (self->global_noncore_names);
  g_hash_table_unref (self->custom_label_map);
  g_hash_table_unref (self->category_label_names);

  G_OBJECT_CLASS (cz_custom_label_store_parent_class)->finalize (object);
}

static void
cz_custom_label_store_class_init (CzCustomLabelStoreClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = cz_custom_label_store_finalize;

  signals[CHANGED_SIGNAL] = g_signal_new ("changed",
                                          CZ_TYPE_CUSTOM_LABEL_STORE,
                                          G_SIGNAL_RUN_LAST,
                                          0, NULL, NULL, NULL,
                                          G_TYPE_NONE, 0);
}

static void
cz_custom_label_store_init (CzCustomLabelStore *self)
{
  self->core_map             = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                      g_free, g_free);
  self->noncore_map          = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                      g_free, (GDestroyNotify) g_hash_table_unref);
  self->global_noncore_names = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                      g_free, NULL);
  self->custom_label_map     = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                      g_free,
                                                      custom_label_assignment_free);
  self->category_label_names = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                      g_free, (GDestroyNotify) g_hash_table_unref);
}

CzCustomLabelStore *
cz_custom_label_store_new (void)
{
  return g_object_new (CZ_TYPE_CUSTOM_LABEL_STORE, NULL);
}
