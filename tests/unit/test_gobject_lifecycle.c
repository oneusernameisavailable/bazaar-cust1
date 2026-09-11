/*
 * GObject lifecycle tests.
 *
 * Verifies:
 *   - bz_entry_hold / bz_entry_release balance correctly
 *   - bz_entry_is_holding returns true after hold
 *   - g_clear_object sets pointer to NULL
 *   - Transaction creation does not crash
 *   - Merging transactions does not crash
 */

#include "bz-entry.h"
#include "bz-transaction.h"

static BzEntry *test_entry;

static void
test_entry_create_destroy (void)
{
  BzEntry *entry;

  entry = bz_entry_new (NULL);
  g_test_message ("entry=%p type=%s",
                  (void *) entry, entry ? G_OBJECT_TYPE_NAME (entry) : "(null)");
  g_assert_nonnull (entry);

  g_clear_object (&entry);
  g_test_message ("entry after unref: %p", (void *) entry);
  g_assert_null (entry);
}

static void
test_entry_is_holding_false_initially (void)
{
  BzEntry *entry;
  gboolean holding;

  entry = bz_entry_new (NULL);
  g_assert_nonnull (entry);

  holding = bz_entry_is_holding (entry);
  g_test_message ("bz_entry_is_holding fresh entry: %d", holding);
  g_assert_false (holding);

  g_clear_object (&entry);
}

static void
test_entry_hold_is_holding_cycle (void)
{
  BzEntry *entry;
  gboolean holding;

  entry = bz_entry_new (NULL);
  g_assert_nonnull (entry);

  g_assert_false (bz_entry_is_holding (entry));
  bz_entry_hold (entry);
  g_assert_true (bz_entry_is_holding (entry));
  bz_entry_release (entry);
  holding = bz_entry_is_holding (entry);
  g_test_message ("after hold+release, is_holding=%d", holding);
  g_assert_false (holding);

  g_clear_object (&entry);
}

static void
test_entry_clear_object_sets_null (void)
{
  BzEntry *entry;

  entry = bz_entry_new (NULL);
  g_assert_nonnull (entry);
  g_clear_object (&entry);
  g_test_message ("after g_clear_object, entry=%p", (void *) entry);
  g_assert_null (entry);
}

static void
test_transaction_create_destroy (void)
{
  BzTransaction *tx;

  tx = bz_transaction_new_full (&test_entry, 1, NULL, 0, NULL, 0);
  g_test_message ("tx=%p type=%s",
                  (void *) tx, G_OBJECT_TYPE_NAME (tx));
  g_assert_nonnull (tx);
  g_assert_true (BZ_IS_TRANSACTION (tx));

  g_clear_object (&tx);
}

static void
test_transaction_hold_release_cycle (void)
{
  BzTransaction *tx;

  tx = bz_transaction_new_full (&test_entry, 1, NULL, 0, NULL, 0);
  g_assert_nonnull (tx);

  g_test_message ("calling bz_transaction_hold");
  bz_transaction_hold (tx);
  g_test_message ("calling bz_transaction_release");
  bz_transaction_release (tx);

  g_clear_object (&tx);
}

static void
test_transaction_merge_two_empty (void)
{
  BzTransaction *tx1, *tx2, *merged;
  BzTransaction *txs[2];

  tx1    = bz_transaction_new_full (&test_entry, 1, NULL, 0, NULL, 0);
  tx2    = bz_transaction_new_full (&test_entry, 1, NULL, 0, NULL, 0);
  txs[0] = tx1;
  txs[1] = tx2;

  merged = bz_transaction_new_merged (txs, 2);
  g_test_message ("merged tx=%p type=%s", (void *) merged,
                  merged ? G_OBJECT_TYPE_NAME (merged) : "(null)");
  g_assert_nonnull (merged);

  g_clear_object (&tx1);
  g_clear_object (&tx2);
  g_clear_object (&merged);
}

int
main (int argc, char *argv[])
{
  int ret;

  g_test_init (&argc, &argv, NULL);
  g_test_set_nonfatal_assertions ();

  test_entry = bz_entry_new (NULL);
  g_assert_nonnull (test_entry);

  g_test_add_func ("/gobject-lifecycle/entry-create-destroy",
                   test_entry_create_destroy);
  g_test_add_func ("/gobject-lifecycle/entry-is-holding-false-initially",
                   test_entry_is_holding_false_initially);
  g_test_add_func ("/gobject-lifecycle/entry-hold-is-holding-cycle",
                   test_entry_hold_is_holding_cycle);
  g_test_add_func ("/gobject-lifecycle/entry-clear-object-null",
                   test_entry_clear_object_sets_null);
  g_test_add_func ("/gobject-lifecycle/transaction-create-destroy",
                   test_transaction_create_destroy);
  g_test_add_func ("/gobject-lifecycle/transaction-hold-release",
                   test_transaction_hold_release_cycle);
  g_test_add_func ("/gobject-lifecycle/transaction-merge",
                   test_transaction_merge_two_empty);

  ret = g_test_run ();

  g_clear_object (&test_entry);

  return ret;
}
