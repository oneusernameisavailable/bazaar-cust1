/*
 * Data graph widget tests.
 *
 * Verifies:
 *   - BzDataGraph creates as GtkWidget
 *   - Axis label get/set round-trip
 *   - Decimals get/set round-trip
 *   - Transition progress get/set round-trip
 *   - animate_open does not crash
 *   - Tooltip prefix get/set round-trip
 *   - Model set to NULL does not crash
 */

#include "bz-data-graph.h"

static void
test_data_graph_new_returns_widget (void)
{
  GtkWidget *graph;

  graph = bz_data_graph_new ();
  g_object_ref_sink (graph);
  g_test_message ("graph=%p type=%s", (void *) graph,
                  graph ? G_OBJECT_TYPE_NAME (graph) : "(null)");
  g_assert_nonnull (graph);
  g_assert_true (BZ_IS_DATA_GRAPH (graph));
  g_assert_true (GTK_IS_WIDGET (graph));

  g_clear_object (&graph);
}

static void
test_data_graph_independent_axis_label_roundtrip (void)
{
  GtkWidget  *graph;
  const char *label;

  graph = bz_data_graph_new ();
  g_object_ref_sink (graph);
  g_assert_nonnull (graph);

  bz_data_graph_set_independent_axis_label (BZ_DATA_GRAPH (graph), "Test Label");
  label = bz_data_graph_get_independent_axis_label (BZ_DATA_GRAPH (graph));
  g_test_message ("independent label=\"%s\"", label);
  g_assert_nonnull (label);
  g_assert_cmpstr (label, ==, "Test Label");

  g_clear_object (&graph);
}

static void
test_data_graph_dependent_axis_label_roundtrip (void)
{
  GtkWidget  *graph;
  const char *label;

  graph = bz_data_graph_new ();
  g_object_ref_sink (graph);
  g_assert_nonnull (graph);

  bz_data_graph_set_dependent_axis_label (BZ_DATA_GRAPH (graph), "Dependency Count");
  label = bz_data_graph_get_dependent_axis_label (BZ_DATA_GRAPH (graph));
  g_test_message ("dependent label=\"%s\"", label);
  g_assert_nonnull (label);
  g_assert_cmpstr (label, ==, "Dependency Count");

  g_clear_object (&graph);
}

static void
test_data_graph_decimals_roundtrip (void)
{
  GtkWidget *graph;
  gint       indep, dep;

  graph = bz_data_graph_new ();
  g_object_ref_sink (graph);
  g_assert_nonnull (graph);

  bz_data_graph_set_independent_decimals (BZ_DATA_GRAPH (graph), 2);
  indep = bz_data_graph_get_independent_decimals (BZ_DATA_GRAPH (graph));
  g_test_message ("independent decimals=%d (expected=2)", indep);
  g_assert_cmpint (indep, ==, 2);

  bz_data_graph_set_dependent_decimals (BZ_DATA_GRAPH (graph), 3);
  dep = bz_data_graph_get_dependent_decimals (BZ_DATA_GRAPH (graph));
  g_test_message ("dependent decimals=%d (expected=3)", dep);
  g_assert_cmpint (dep, ==, 3);

  g_clear_object (&graph);
}

static void
test_data_graph_transition_progress_roundtrip (void)
{
  GtkWidget *graph;
  gfloat     prog;

  graph = bz_data_graph_new ();
  g_object_ref_sink (graph);
  g_assert_nonnull (graph);

  bz_data_graph_set_transition_progress (BZ_DATA_GRAPH (graph), 0.5);
  prog = bz_data_graph_get_transition_progress (BZ_DATA_GRAPH (graph));
  g_test_message ("progress=%.2f (expected=0.50)", (double) prog);
  g_assert_cmpfloat (prog, ==, 0.5);

  bz_data_graph_set_transition_progress (BZ_DATA_GRAPH (graph), 0.0);
  prog = bz_data_graph_get_transition_progress (BZ_DATA_GRAPH (graph));
  g_test_message ("progress=%.2f (expected=0.00)", (double) prog);
  g_assert_cmpfloat (prog, ==, 0.0);

  bz_data_graph_set_transition_progress (BZ_DATA_GRAPH (graph), 1.0);
  prog = bz_data_graph_get_transition_progress (BZ_DATA_GRAPH (graph));
  g_test_message ("progress=%.2f (expected=1.00)", (double) prog);
  g_assert_cmpfloat (prog, ==, 1.0);

  g_clear_object (&graph);
}

static void
test_data_graph_animate_open_does_not_crash (void)
{
  GtkWidget *graph;

  graph = bz_data_graph_new ();
  g_object_ref_sink (graph);
  g_assert_nonnull (graph);

  g_test_message ("calling bz_data_graph_animate_open");
  bz_data_graph_animate_open (BZ_DATA_GRAPH (graph));

  g_clear_object (&graph);
}

static void
test_data_graph_tooltip_prefix_roundtrip (void)
{
  GtkWidget  *graph;
  const char *prefix;

  graph = bz_data_graph_new ();
  g_object_ref_sink (graph);
  g_assert_nonnull (graph);

  bz_data_graph_set_tooltip_prefix (BZ_DATA_GRAPH (graph), "Installs: ");
  prefix = bz_data_graph_get_tooltip_prefix (BZ_DATA_GRAPH (graph));
  g_test_message ("tooltip prefix=\"%s\"", prefix);
  g_assert_nonnull (prefix);
  g_assert_cmpstr (prefix, ==, "Installs: ");

  g_clear_object (&graph);
}

static void
test_data_graph_set_model_null (void)
{
  GtkWidget  *graph;
  GListModel *model;

  graph = bz_data_graph_new ();
  g_object_ref_sink (graph);
  g_assert_nonnull (graph);

  bz_data_graph_set_model (BZ_DATA_GRAPH (graph), NULL);
  model = bz_data_graph_get_model (BZ_DATA_GRAPH (graph));
  g_test_message ("model after set(NULL)=%p", (void *) model);
  g_assert_null (model);

  g_clear_object (&graph);
}

int
main (int argc, char *argv[])
{
  gtk_test_init (&argc, &argv, NULL);
  g_test_set_nonfatal_assertions ();

  g_test_add_func ("/data-graph/new-returns-widget",
                   test_data_graph_new_returns_widget);
  g_test_add_func ("/data-graph/independent-axis-label",
                   test_data_graph_independent_axis_label_roundtrip);
  g_test_add_func ("/data-graph/dependent-axis-label",
                   test_data_graph_dependent_axis_label_roundtrip);
  g_test_add_func ("/data-graph/decimals-roundtrip",
                   test_data_graph_decimals_roundtrip);
  g_test_add_func ("/data-graph/transition-progress",
                   test_data_graph_transition_progress_roundtrip);
  g_test_add_func ("/data-graph/animate-open",
                   test_data_graph_animate_open_does_not_crash);
  g_test_add_func ("/data-graph/tooltip-prefix",
                   test_data_graph_tooltip_prefix_roundtrip);
  g_test_add_func ("/data-graph/set-model-null",
                   test_data_graph_set_model_null);

  return g_test_run ();
}
