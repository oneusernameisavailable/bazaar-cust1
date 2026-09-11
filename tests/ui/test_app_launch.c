/* test_app_launch.c
 *
 * Regression test for launching the built bazaar binary the way a file
 * manager would: with a minimal $PATH, so the refresh/download workers are
 * not found via PATH. The app must find its workers next to its own binary
 * and stay alive instead of aborting.
 */

#include <gio/gio.h>
#include <glib.h>

static gboolean
app_stays_alive (const char *app_path,
                 int         seconds)
{
  g_autoptr (GSubprocessLauncher) launcher = NULL;
  g_autoptr (GSubprocess) proc             = NULL;
  g_autoptr (GError) error                 = NULL;
  g_autofree char *tmp_dir                 = NULL;
  g_autofree char *xdg_data_home           = NULL;
  g_autofree char *xdg_cache_home          = NULL;
  g_autofree char *xdg_config_home         = NULL;
  g_autofree char *state_dir               = NULL;
  gint             i;

  tmp_dir = g_dir_make_tmp ("bz-launch-XXXXXX", &error);
  if (tmp_dir == NULL)
    {
      g_printerr ("could not create temp dir: %s\n", error->message);
      return FALSE;
    }

  xdg_data_home   = g_build_filename (tmp_dir, "data", NULL);
  xdg_cache_home  = g_build_filename (tmp_dir, "cache", NULL);
  xdg_config_home = g_build_filename (tmp_dir, "config", NULL);
  state_dir       = g_build_filename (tmp_dir, "state", NULL);

  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);
  g_subprocess_launcher_setenv (launcher, "PATH", "/usr/bin:/bin", TRUE);
  g_subprocess_launcher_setenv (launcher, "XDG_DATA_HOME", xdg_data_home, TRUE);
  g_subprocess_launcher_setenv (launcher, "XDG_CACHE_HOME", xdg_cache_home, TRUE);
  g_subprocess_launcher_setenv (launcher, "XDG_CONFIG_HOME", xdg_config_home, TRUE);
  g_subprocess_launcher_setenv (launcher, "XDG_STATE_HOME", state_dir, TRUE);
  g_subprocess_launcher_setenv (launcher, "GSETTINGS_BACKEND", "memory", TRUE);
  g_subprocess_launcher_setenv (launcher, "GSK_RENDERER", "cairo", TRUE);
  g_subprocess_launcher_setenv (launcher, "GTK_A11Y", "test", TRUE);
  /* Disable sanitizer abort-on-error for the spawned process, as sanitizers
     can interfere with GResource registration during startup. */
  g_subprocess_launcher_setenv (launcher, "ASAN_OPTIONS", "halt_on_error=0:abort_on_error=0", TRUE);
  g_subprocess_launcher_setenv (launcher, "MSAN_OPTIONS", "halt_on_error=0:abort_on_error=0", TRUE);
  g_subprocess_launcher_setenv (launcher, "UBSAN_OPTIONS", "halt_on_error=0:abort_on_error=0", TRUE);

  proc = g_subprocess_launcher_spawn (launcher, &error, app_path, NULL);
  if (proc == NULL)
    {
      g_printerr ("could not spawn %s: %s\n", app_path, error->message);
      return FALSE;
    }

  for (i = 0; i < seconds * 2; i++)
    {
      g_usleep (500000);
      if (g_subprocess_get_if_exited (proc) || g_subprocess_get_if_signaled (proc))
        {
          g_printerr ("%s exited after %d.%ds (status=%d, signal=%d)\n",
                      app_path, i / 2, 5 * (i % 2),
                      g_subprocess_get_exit_status (proc),
                      g_subprocess_get_term_sig (proc));
          return FALSE;
        }
    }

  g_print ("OK: %s survived startup (checked %d times)\n", app_path, i);
  g_subprocess_force_exit (proc);
  g_subprocess_wait (proc, NULL, NULL);

  return TRUE;
}

int
main (int argc, char *argv[])
{
  gboolean ok = TRUE;
  int      i;

  if (g_getenv ("DISPLAY") == NULL && g_getenv ("WAYLAND_DISPLAY") == NULL)
    {
      g_print ("SKIP: no display available\n");
      return 77;
    }

  for (i = 1; i < argc; i++)
    {
      g_print ("testing %s...\n", argv[i]);
      if (!app_stays_alive (argv[i], 20))
        ok = FALSE;
    }

  return ok ? 0 : 1;
}