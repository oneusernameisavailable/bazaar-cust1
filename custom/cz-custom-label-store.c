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

struct _CzCustomLabelStore
{
  GObject       parent_instance;
  GHashTable   *core_map;             /* app_id → g_strdup'd label string */
  GHashTable   *noncore_map;          /* app_id → GHashTable<label, NULL> */
  GHashTable   *global_noncore_names; /* label → NULL (set of all known noncore label names) */
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

gboolean
cz_custom_label_store_has_noncore_label (CzCustomLabelStore *self,
                                         const char         *app_id,
                                         const char         *label)
{
  GHashTable *set;

  g_return_val_if_fail (CZ_IS_CUSTOM_LABEL_STORE (self), FALSE);
  g_return_val_if_fail (app_id != NULL, FALSE);
  g_return_val_if_fail (label != NULL, FALSE);

  set = (GHashTable *) g_hash_table_lookup (self->noncore_map, app_id);
  if (set == NULL)
    return FALSE;

  return label_set_contains (set, label);
}

void
cz_custom_label_store_add_noncore_label (CzCustomLabelStore *self,
                                         const char         *app_id,
                                         const char         *label)
{
  GHashTable *set;

  g_return_if_fail (CZ_IS_CUSTOM_LABEL_STORE (self));
  g_return_if_fail (app_id != NULL);
  g_return_if_fail (label != NULL);

  set = (GHashTable *) g_hash_table_lookup (self->noncore_map, app_id);
  if (set == NULL)
    {
      set = make_label_set ();
      g_hash_table_insert (self->noncore_map, g_strdup (app_id), set);
    }

  if (!label_set_contains (set, label))
    {
      g_hash_table_add (set, g_strdup (label));
      if (!g_hash_table_contains (self->global_noncore_names, label))
        g_hash_table_add (self->global_noncore_names, g_strdup (label));
      if (self->store != NULL)
        bz_label_store_add_noncore_label (self->store, app_id, label, NULL);
      g_signal_emit (self, signals[CHANGED_SIGNAL], 0);
    }
}

void
cz_custom_label_store_remove_noncore_label (CzCustomLabelStore *self,
                                            const char         *app_id,
                                            const char         *label)
{
  GHashTable *set;

  g_return_if_fail (CZ_IS_CUSTOM_LABEL_STORE (self));
  g_return_if_fail (app_id != NULL);
  g_return_if_fail (label != NULL);

  set = (GHashTable *) g_hash_table_lookup (self->noncore_map, app_id);
  if (set == NULL)
    return;

  if (label_set_contains (set, label))
    {
      g_hash_table_remove (set, label);
      if (self->store != NULL)
        bz_label_store_remove_noncore_label (self->store, app_id, label, NULL);
      g_signal_emit (self, signals[CHANGED_SIGNAL], 0);
    }
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

GPtrArray *
cz_custom_label_store_get_all_noncore_label_names (CzCustomLabelStore *self)
{
  GPtrArray     *names;
  GHashTableIter iter;
  gpointer       k;

  g_return_val_if_fail (CZ_IS_CUSTOM_LABEL_STORE (self), NULL);

  names = g_ptr_array_new_with_free_func (g_free);
  g_hash_table_iter_init (&iter, self->global_noncore_names);
  while (g_hash_table_iter_next (&iter, &k, NULL))
    g_ptr_array_add (names, g_strdup ((const char *) k));

  return names;
}

gboolean
cz_custom_label_store_add_noncore_label_name (CzCustomLabelStore *self,
                                              const char         *name)
{
  g_return_val_if_fail (CZ_IS_CUSTOM_LABEL_STORE (self), FALSE);
  g_return_val_if_fail (name != NULL, FALSE);

  if (g_hash_table_contains (self->global_noncore_names, name))
    return FALSE;

  g_hash_table_add (self->global_noncore_names, g_strdup (name));
  if (self->store != NULL)
    {
      g_autoptr (GError) err = NULL;
      if (!bz_label_store_add_label_name (self->store, name, &err))
        g_warning ("Failed to persist label name '%s' to database: %s",
                   name, err->message);
    }
  else
    {
      g_warning ("Attempted to add label name '%s' but database store is NULL",
                 name);
    }
  g_signal_emit (self, signals[CHANGED_SIGNAL], 0);
  return TRUE;
}

gboolean
cz_custom_label_store_remove_noncore_label_name (CzCustomLabelStore *self,
                                                 const char         *name)
{
  GHashTableIter map_iter;
  gpointer       key, value;
  gboolean       was_present;

  g_return_val_if_fail (CZ_IS_CUSTOM_LABEL_STORE (self), FALSE);
  g_return_val_if_fail (name != NULL, FALSE);

  was_present = g_hash_table_remove (self->global_noncore_names, name);

  /* Also remove this label from all app noncore sets */
  g_hash_table_iter_init (&map_iter, self->noncore_map);
  while (g_hash_table_iter_next (&map_iter, &key, &value))
    {
      GHashTable *set = (GHashTable *) value;
      g_hash_table_remove (set, name);
    }

  if (self->store != NULL)
    bz_label_store_remove_label_name (self->store, name, NULL);

  g_signal_emit (self, signals[CHANGED_SIGNAL], 0);
  return was_present;
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
}

CzCustomLabelStore *
cz_custom_label_store_new (void)
{
  return g_object_new (CZ_TYPE_CUSTOM_LABEL_STORE, NULL);
}
