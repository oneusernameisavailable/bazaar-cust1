/*
 * Search engine tests.
 *
 * Verifies:
 *   - Engine created with NULL model/biases does not crash
 *   - Setting model is idempotent
 *   - Query with NULL terms does not crash
 *   - Query with empty terms does not crash
 */

#include "bz-entry-group.h"
#include "bz-entry.h"
#include "bz-search-engine.h"

static GListStore *
make_test_entries (void)
{
  GListStore *store;
  BzEntry    *e1, *e2;

  store = g_list_store_new (BZ_TYPE_ENTRY);
  g_assert_nonnull (store);

  e1 = bz_entry_new (NULL);
  e2 = bz_entry_new (NULL);

  g_list_store_append (store, e1);
  g_list_store_append (store, e2);

  g_clear_object (&e1);
  g_clear_object (&e2);

  return store;
}

static void
test_search_engine_create_destroy (void)
{
  BzSearchEngine *engine;

  engine = bz_search_engine_new ();
  g_test_message ("engine=%p type=%s", (void *) engine,
                  engine ? G_OBJECT_TYPE_NAME (engine) : "(null)");
  g_assert_nonnull (engine);
  g_assert_true (BZ_IS_SEARCH_ENGINE (engine));

  g_clear_object (&engine);
}

static void
test_search_engine_get_model_returns_null_initially (void)
{
  BzSearchEngine *engine;
  GListModel     *model;

  engine = bz_search_engine_new ();
  model  = bz_search_engine_get_model (engine);
  g_test_message ("initial model=%p", (void *) model);
  g_assert_null (model);

  g_clear_object (&engine);
}

static void
test_search_engine_set_model_roundtrip (void)
{
  BzSearchEngine *engine;
  GListStore     *store;
  GListModel     *model;

  engine = bz_search_engine_new ();
  store  = make_test_entries ();

  bz_search_engine_set_model (engine, G_LIST_MODEL (store));
  model = bz_search_engine_get_model (engine);
  g_test_message ("model after set=%p (expected=%p)", (void *) model, (void *) store);
  g_assert_nonnull (model);
  g_assert_true (model == G_LIST_MODEL (store));

  g_clear_object (&store);
  g_clear_object (&engine);
}

static void
test_search_engine_query_null_terms (void)
{
  BzSearchEngine *engine;
  DexFuture      *result;

  engine = bz_search_engine_new ();
  result = bz_search_engine_query (engine, NULL);
  g_test_message ("query(NULL terms) returned future=%p", (void *) result);
  g_assert_nonnull (result);

  g_clear_pointer (&result, dex_unref);
  g_clear_object (&engine);
}

static void
test_search_engine_query_empty_terms (void)
{
  BzSearchEngine *engine;
  DexFuture      *result;
  const char     *empty_terms[] = { NULL };

  engine = bz_search_engine_new ();
  result = bz_search_engine_query (engine, empty_terms);
  g_test_message ("query(empty terms) returned future=%p", (void *) result);
  g_assert_nonnull (result);

  g_clear_pointer (&result, dex_unref);
  g_clear_object (&engine);
}

static void
test_search_engine_set_model_null (void)
{
  BzSearchEngine *engine;
  GListModel     *model;

  engine = bz_search_engine_new ();

  bz_search_engine_set_model (engine, NULL);
  model = bz_search_engine_get_model (engine);
  g_test_message ("model after set(NULL)=%p", (void *) model);
  g_assert_null (model);

  g_clear_object (&engine);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_set_nonfatal_assertions ();

  g_test_add_func ("/search-engine/create-destroy",
                   test_search_engine_create_destroy);
  g_test_add_func ("/search-engine/model-null-initially",
                   test_search_engine_get_model_returns_null_initially);
  g_test_add_func ("/search-engine/set-model-roundtrip",
                   test_search_engine_set_model_roundtrip);
  g_test_add_func ("/search-engine/query-null-terms",
                   test_search_engine_query_null_terms);
  g_test_add_func ("/search-engine/query-empty-terms",
                   test_search_engine_query_empty_terms);
  g_test_add_func ("/search-engine/set-model-null",
                   test_search_engine_set_model_null);

  return g_test_run ();
}
