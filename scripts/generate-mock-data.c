/*
 * generate-mock-data.c
 *
 * Generates realistic mock Flathub entries for local UI testing.
 * Outputs a JSON array of app entries that can be loaded by a test backend.
 *
 * Usage: meson compile -C build && build/scripts/generate-mock-data > tests/mock/flathub-apps.json
 *
 * Each entry mimicks the fields BzEntry reads from AppStream metadata.
 */

#include <json-glib/json-glib.h>
#include <stdio.h>
#include <string.h>

static const char *app_names[] = {
  "Firefox", "Chromium", "GIMP", "Inkscape", "LibreOffice",
  "VLC", "Spotify", "Blender", "OBS Studio", "Thunderbird",
  "Element", "Signal", "Krita", "Audacity", "HandBrake",
  "GNOME Calculator", "GNOME Calendar", "GNOME Maps", "GNOME Weather", "GNOME Text Editor",
  "Fractal", "Pika Backup", "Resources", "NewsFlash", "Cozy",
  NULL
};

static const char *app_ids[] = {
  "org.mozilla.firefox", "org.chromium.Chromium", "org.gimp.GIMP", "org.inkscape.Inkscape",
  "org.libreoffice.LibreOffice", "org.videolan.VLC", "com.spotify.Client", "org.blender.Blender",
  "com.obsproject.Studio", "org.mozilla.thunderbird", "im.riot.Riot", "org.signal.Signal",
  "org.kde.krita", "org.audacityteam.Audacity", "fr.handbrake.ghb",
  "org.gnome.Calculator", "org.gnome.Calendar", "org.gnome.Maps", "org.gnome.Weather",
  "org.gnome.TextEditor", "org.gnome.Fractal", "org.gnome.Pika", "net.nokyan.Resources",
  "com.gitlab.newsflash", "io.github.cthomas1.Cozy",
  NULL
};

static const char *developers[] = {
  "Mozilla", "The Chromium Authors", "The GIMP Team", "Inkscape Developers",
  "The Document Foundation", "VideoLAN", "Spotify AB", "Blender Foundation",
  "OBS Project", "Mozilla Foundation", "Element", "Signal Messenger",
  "KDE", "Audacity Team", "HandBrake Team",
  "GNOME Project", "GNOME Project", "GNOME Project", "GNOME Project", "GNOME Project",
  "GNOME Project", "GNOME Project", "Nokyan", "NewsFlash Team", "Cozy Team",
  NULL
};

static const char *categories[] = {
  "Network;WebBrowser;", "Network;WebBrowser;", "Graphics;Photography;",
  "Graphics;VectorGraphics;", "Office;WordProcessor;",
  "AudioVideo;Player;", "AudioVideo;Audio;", "Graphics;3DGraphics;",
  "AudioVideo;Video;", "Network;Email;",
  "Network;InstantMessaging;", "Network;InstantMessaging;",
  "Graphics;RasterGraphics;", "AudioVideo;AudioEditor;", "AudioVideo;VideoConverter;",
  "Utility;Calculator;", "Office;Calendar;", "Maps;Navigation;", "Weather;Utility;",
  "Utility;TextEditor;", "Network;InstantMessaging;", "Utility;Backup;",
  "System;Monitor;", "Network;RSS;", "AudioVideo;Audio;",
  NULL
};

static const char *summaries[] = {
  "Fast, private web browser", "Open-source web browser", "Image manipulation program",
  "Professional vector graphics editor", "Free office suite",
  "Media player for all formats", "Music streaming service", "3D creation suite",
  "Live streaming and recording", "Email, calendar and news client",
  "Decentralized secure messenger", "Encrypted messenger",
  "Digital painting and illustration", "Multi-track audio editor", "Video transcoder",
  "Simple calculator", "Calendar with events and tasks", "Explore the world map",
  "Weather forecast", "Simple text editor",
  "Matrix chat client", "Back up your files", "System resource monitor",
  "RSS feed reader", "Lossless audio music player",
  NULL
};

static const guint64 sizes[] = {
  250000000, 200000000, 150000000, 180000000, 600000000,
  80000000, 120000000, 400000000, 200000000, 300000000,
  50000000, 60000000, 250000000, 30000000, 80000000,
  5000000, 10000000, 50000000, 15000000, 8000000,
  40000000, 70000000, 35000000, 20000000, 45000000,
};

static const double ratings[] = {
  4.5, 4.2, 4.7, 4.4, 4.3,
  4.6, 4.1, 4.8, 4.5, 4.2,
  4.3, 4.6, 4.7, 4.0, 4.4,
  4.2, 4.1, 4.3, 4.0, 4.5,
  4.1, 4.4, 4.6, 4.3, 4.2,
};

int
main (void)
{
  JsonBuilder *builder;
  guint i;

  builder = json_builder_new ();
  json_builder_begin_object (builder);
  json_builder_set_member_name (builder, "apps");
  json_builder_begin_array (builder);

  for (i = 0; app_names[i] != NULL; i++) {
    json_builder_begin_object (builder);

    json_builder_set_member_name (builder, "id");
    json_builder_add_string_value (builder, app_ids[i]);

    json_builder_set_member_name (builder, "name");
    json_builder_add_string_value (builder, app_names[i]);

    json_builder_set_member_name (builder, "developer");
    json_builder_add_string_value (builder, developers[i]);

    json_builder_set_member_name (builder, "summary");
    json_builder_add_string_value (builder, summaries[i]);

    json_builder_set_member_name (builder, "categories");
    json_builder_add_string_value (builder, categories[i]);

    json_builder_set_member_name (builder, "size");
    json_builder_add_int_value (builder, (gint64) sizes[i]);

    json_builder_set_member_name (builder, "rating");
    json_builder_add_double_value (builder, ratings[i]);

    json_builder_set_member_name (builder, "is_flathub");
    json_builder_add_boolean_value (builder, TRUE);

    json_builder_set_member_name (builder, "is_verified");
    json_builder_add_boolean_value (builder, i < 10);

    json_builder_set_member_name (builder, "is_foss");
    json_builder_add_boolean_value (builder, i != 6 && i != 5);

    json_builder_end_object (builder);
  }

  json_builder_end_array (builder);
  json_builder_end_object (builder);

  {
    JsonGenerator *gen;
    JsonNode *root;

    root = json_builder_get_root (builder);
    gen = json_generator_new ();
    json_generator_set_pretty (gen, TRUE);
    json_generator_set_root (gen, root);

    printf ("%s\n", json_generator_to_data (gen, NULL));

    g_object_unref (gen);
    json_node_free (root);
  }

  g_object_unref (builder);
  return 0;
}
