/*
 * Entry cache manager tests.
 *
 * Verifies:
 *   - BzEntryCacheManager creates and destroys without error
 *   - Living entries count is zero on creation
 *   - GType is correct
 */

#include "bz-entry-cache-manager.h"
#include "bz-entry.h"

static void
test_entry_cache_manager_create_destroy (void)
{
  BzEntryCacheManager *manager;

  manager = bz_entry_cache_manager_new ();
  g_test_message ("manager=%p type=%s", (void *) manager,
                  manager ? G_OBJECT_TYPE_NAME (manager) : "(null)");
  g_assert_nonnull (manager);
  g_assert_true (BZ_IS_ENTRY_CACHE_MANAGER (manager));

  g_clear_object (&manager);
  g_test_message ("manager after clear=%p", (void *) manager);
  g_assert_null (manager);
}

static void
test_entry_cache_manager_living_entries_zero_on_create (void)
{
  BzEntryCacheManager *manager;

  manager      = bz_entry_cache_manager_new ();
  guint living = bz_entry_cache_manager_get_living_entries (manager);
  g_test_message ("living entries=%u (expected=0)", living);
  g_assert_cmpuint (living, ==, 0);

  g_clear_object (&manager);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_set_nonfatal_assertions ();

  g_test_add_func ("/entry-cache-manager/create-destroy",
                   test_entry_cache_manager_create_destroy);
  g_test_add_func ("/entry-cache-manager/living-entries-zero",
                   test_entry_cache_manager_living_entries_zero_on_create);

  return g_test_run ();
}
