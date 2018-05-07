/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */


#include <glib.h>
#include <glib/gprintf.h>
#include <gio/gio.h>

#include <stdio.h>


static GRegex *global_replace_re = NULL;


static void on_error (const gchar *string) {
  g_fprintf(stderr, "%s\n", string);
}


static void free_pointer_at(gpointer ptr) {
  g_free (*(gpointer*)ptr);
}


static GArray *load_config_dirs () {
  g_autoptr(GArray) dirs = g_array_new (FALSE, FALSE, sizeof(gchar *));
  g_array_set_clear_func (dirs, free_pointer_at);

  const gchar *extra = g_getenv ("DTWEAKS_PATH");
  if (extra != NULL) {
    g_debug ("Read DTWEAKS_PATH: %s", extra);
    g_autofree gchar **parts = g_strsplit (extra, ":", -1);
    g_array_append_vals (dirs, parts, g_strv_length (parts));
  }

  gchar *default_dirs[] = {
    g_strdup ("/etc/dtweaks.d"),
    g_strdup ("/usr/local/share/dtweaks.d"),
    g_strdup ("/usr/share/dtweaks.d"),
  };
  g_array_append_vals (dirs, default_dirs, G_N_ELEMENTS(default_dirs));

  return g_steal_pointer (&dirs);
}


static GFile *find_tweak (GArray *config_dirs, const gchar *basename) {
  for (gint i = 0; i < config_dirs->len; i++) {
    gchar *dirstring = g_array_index (config_dirs, gchar *, i);
    g_autoptr(GFile) dir = g_file_new_for_path (dirstring);
    g_autoptr(GFile) tweaks_path = g_file_get_child (dir, basename);

    g_debug ("Searching for tweak for %s: %s", basename, g_file_peek_path (tweaks_path));
    if (g_file_query_exists (tweaks_path, NULL)) {
      return g_steal_pointer (&tweaks_path);
    }
  }

  g_debug ("Search failed.");
  return NULL;
}


static GKeyFile *load_key_file (GFile *path, GKeyFileFlags flags) {
  g_autoptr(GKeyFile) kf = g_key_file_new ();
  const gchar *path_s = g_file_peek_path (path);
  g_autoptr(GError) error = NULL;

  if (!g_key_file_load_from_file (kf, path_s, flags, &error))  {
    g_printerr ("Failed to load %s: %s", path_s, error->message);
    return NULL;
  }

  return g_steal_pointer (&kf);
}


typedef struct KeyTransformer KeyTransformer;
struct KeyTransformer {
  gchar *template;
  GRegex *re;
};


static void key_transformer_free (KeyTransformer *kt) {
  g_free (kt->template);
  if (kt->re) {
    g_regex_unref (kt->re);
  }
  g_free (kt);
}


typedef struct GroupTransformer GroupTransformer;
struct GroupTransformer {
  gchar *name;
  GPatternSpec *pattern;
  GHashTable *kts;
};


static void group_transformer_free (GroupTransformer *gt) {
  g_free (gt->name);
  g_pattern_spec_free (gt->pattern);
  g_hash_table_unref (gt->kts);
  g_free (gt);
}


typedef struct ReplacerData ReplacerData;
struct ReplacerData {
  gchar *value;
  GMatchInfo *match;
};


static gboolean replacer_callback(const GMatchInfo *m, GString *res, gpointer pdata) {
  ReplacerData *data = pdata;

  g_autofree gchar *var1 = g_match_info_fetch (m, 1);
  g_autofree gchar *var2 = g_match_info_fetch (m, 2);
  g_autofree gchar *ignore = g_match_info_fetch (m, 3);

  gchar *var = var1[0] ? var1 : var2[0] ? var2 : NULL;
  if (var == NULL) {
    g_string_append_c (res, ignore[0]);
  } else if (strcmp (var, "*") == 0) {
    g_string_append (res, data->value);
  } else if (data->match) {
    g_debug ("Replacing variable $%s.", var);
    g_autofree gchar *subst = g_match_info_fetch_named (data->match, var);
    if (subst == NULL && g_ascii_isdigit (var[0])) {
      int group = atoi (var);
      g_debug ("  Recognized as group #%d.", group);
      subst = g_match_info_fetch (data->match, group);
    }

    if (subst != NULL) {
      g_string_append (res, subst);
    }
  }

  return FALSE;
}


static void apply_tweaks (GKeyFile *app, GKeyFile *tweaks, gchar *app_group,
                          GroupTransformer *gt) {
  g_auto(GStrv) keys = g_key_file_get_keys (app, app_group, NULL, NULL);

  for (gchar **p = keys; *p != NULL; p++) {
    gchar *key = *p;
    g_autofree gchar *value = g_key_file_get_value (app, app_group, key, NULL);

    KeyTransformer *kt = g_hash_table_lookup (gt->kts, key);
    if (kt == NULL) {
      g_debug ("Transform missing key %s.", key);
      continue;
    }

    ReplacerData data = {.value = value, .match = NULL};

    if (kt->re) {
      if (!g_regex_match(kt->re, value, 0, &data.match)) {
        g_debug ("XDG desktop file key %s failed to match regex %s.",
                 key, g_regex_get_pattern (kt->re));
        continue;
      }
    }

    g_autofree gchar *newval = g_regex_replace_eval (global_replace_re, kt->template,
                                                     -1, 0, 0, replacer_callback,
                                                     &data, NULL);
    g_key_file_set_value(app, app_group, key, newval);
  }
}


static gboolean process_file (GArray *config_dirs, const gchar *line, gboolean dry_run) {
  g_autoptr(GFile) app_path = g_file_new_for_path (line);
  g_autoptr(GError) error = NULL;
  gboolean success = TRUE;

  g_autofree gchar *basename = g_file_get_basename (app_path);
  g_autoptr(GFile) tweaks_path = find_tweak (config_dirs, basename);
  if (tweaks_path == NULL) {
    return TRUE;
  }

  const gchar *tweaks_path_s = g_file_peek_path (tweaks_path);
  const gchar *app_path_s = g_file_peek_path (app_path);

  g_autoptr(GKeyFile) app = load_key_file (app_path, G_KEY_FILE_KEEP_COMMENTS |
                                                     G_KEY_FILE_KEEP_TRANSLATIONS);
  if (app == NULL) {
    return FALSE;
  }

  g_autoptr(GKeyFile) tweaks = load_key_file (tweaks_path, G_KEY_FILE_NONE);
  if (tweaks == NULL) {
    return FALSE;
  }

  g_auto(GStrv) tweaks_group_names = g_key_file_get_groups (tweaks, NULL);
  g_autoptr(GPtrArray) tweaks_group_transformers =
    g_ptr_array_new_with_free_func ((GDestroyNotify)group_transformer_free);

  for (gchar **p = tweaks_group_names; *p != NULL; p++) {
    gchar *group = *p;

    g_auto(GStrv) keys = g_key_file_get_keys (tweaks, group, NULL, NULL);
    if (keys == NULL) {
      g_warning ("g_key_file_get_keys (%s, %s) failed.", tweaks_path_s, group);
      continue;
    }

    GroupTransformer *gt = g_new (GroupTransformer, 1);
    gt->name = g_strdup (group);
    gt->pattern = g_pattern_spec_new (gt->name);
    gt->kts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                     (GDestroyNotify)key_transformer_free);

    for (gchar **q = keys; *q != NULL; q++) {
      gchar *key = *q;
      gsize keylen = strlen (key);

      gchar *value = g_key_file_get_value (tweaks, gt->name, key, NULL);
      if (value == NULL) {
        g_warning ("g_key_file_get_value (%s, %s, %s) failed.", tweaks_path_s,
                   gt->name, key);
        continue;
      }

      if (key[keylen - 1] == '$') {
        key[keylen - 1] = 0;

        KeyTransformer *kt = g_hash_table_lookup (gt->kts, key);
        if (kt == NULL) {
          g_printerr ("%s: Invalid key %s.%s referenced in regex assignment.",
                      tweaks_path_s, gt->name, key);
          success = FALSE;
          continue;
        } else if (kt->re) {
          g_printerr ("%s: Key %s.%s has more than one regex.",
                      g_file_peek_path (tweaks_path), gt->name, key);
          success = FALSE;
          continue;
        }

        GRegex *re = g_regex_new (value, 0, 0, &error);
        if (re == NULL) {
          g_printerr ("%s: Failed to compile %s.%s$: %s", tweaks_path_s, gt->name, key,
                      error->message);
          success = FALSE;
          continue;
        }

        kt->re = re;
      } else {
        KeyTransformer *kt = g_new (KeyTransformer, 1);
        kt->template = value;
        kt->re = NULL;
        g_hash_table_insert (gt->kts, g_strdup (key), kt);
      }
    }

    g_ptr_array_add (tweaks_group_transformers, gt);
  }

  g_auto(GStrv) app_group_names = g_key_file_get_groups (app, NULL);
  for (gchar **p = app_group_names; *p != NULL; p++) {
    gchar *app_group = *p;

    for (gint i = 0; tweaks_group_names[i] != NULL; i++) {
      GroupTransformer *gt = g_ptr_array_index (tweaks_group_transformers, i);
      g_debug ("Try to match section %s against %s.", app_group, gt->name);
      if (g_pattern_match_string (gt->pattern, app_group)) {
        g_debug ("Match found; applying tweaks.");
        apply_tweaks (app, tweaks, app_group, gt);
        break;
      }
    }
  }

  if (dry_run) {
    g_autofree gchar *data = g_key_file_to_data (app, NULL, NULL);
    g_print ("\n==========%s==========\n\n%s\n", app_path_s, data);
  } else {
    if (!g_key_file_save_to_file (app, app_path_s, &error)) {
      g_printerr("Failed to save %s: %s", app_path_s, error->message);
      success = FALSE;
    }
  }

  return success;
}


static gboolean read_paths_from_stdin (GArray *out) {
  g_autoptr(GIOChannel) gstdin = g_io_channel_unix_new (0);
  g_autoptr(GError) error = NULL;

  gboolean success = TRUE;
  gboolean gstdin_end = FALSE;

  while (!gstdin_end) {
    gchar *line;
    gsize len, endln;

    GIOStatus iostat = g_io_channel_read_line (gstdin, &line, &len, &endln, &error);
    switch (iostat) {
    case G_IO_STATUS_NORMAL:
      if (len == 0) {
        continue;
      }
      line[endln] = 0;
      g_array_append_val (out, line);
      break;
    case G_IO_STATUS_AGAIN:
    case G_IO_STATUS_ERROR:
      g_printerr ("Error reading from stdin: %s", error->message);
      success = FALSE;
      // Fallthrough.
    case G_IO_STATUS_EOF:
      gstdin_end = TRUE;
      break;
    }
  }

  return success;
}


gchar *find_application(const gchar *name) {
  const gchar *const *sysdirs = g_get_system_data_dirs ();
  g_autofree gchar *basename = g_strjoin (".", name, "desktop", NULL);

  for (; *sysdirs != NULL; sysdirs++) {
    g_autoptr(GFile) dir = g_file_new_for_path (*sysdirs);
    g_autoptr(GFile) applications = g_file_get_child (dir, "applications");
    g_autoptr(GFile) path = g_file_get_child (applications, basename);

    g_debug ("Searching for application %s: %s", name, g_file_peek_path (path));

    if (g_file_query_exists (path, NULL)) {
      return g_file_get_path (path);
    }
  }

  g_printerr ("Application %s does not exist.", name);
  return NULL;
}


int main (int argc, char **argv) {
  gboolean opt_stdin = FALSE;
  gboolean opt_resolve_paths = FALSE;
  gboolean opt_dry_run = FALSE;
  gboolean opt_verbose = FALSE;
  GStrv opt_rest = NULL;

  GOptionEntry options[] = {
    {"stdin", 'i', 0, G_OPTION_ARG_NONE, &opt_stdin,
     "Read the list of target XDG application files from stdin, not the command line.",
      NULL},
    {"resolve-paths", 'r', 0, G_OPTION_ARG_NONE, &opt_resolve_paths,
     "Assume non-paths are application names and automatically locate them.", NULL},
    {"dry-run", 'n', 0, G_OPTION_ARG_NONE, &opt_dry_run,
     "Instead of writing to the desktop files, print their transformed contents to the "
     "screen."},
    {"verbose", 'v', 0, G_OPTION_ARG_NONE, &opt_verbose,
     "Show verbose information while processing files."},
    {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &opt_rest, "", NULL},
    {NULL}
  };

  g_set_printerr_handler (on_error);

  global_replace_re = g_regex_new ("\\$\\{(.*?)\\}|\\$(\\w+|\\*)|(\\$\\$|.)", 0, 0,
                                   NULL);
  if (global_replace_re == NULL) {
    g_error ("global_replace_re failed to compile.");
    return 1;
  }

  g_autoptr(GOptionContext) optctx = g_option_context_new (
    "<desktop files>\n"
    "  Automatically modify XDG application files manually or after package "
    "installation.");
  g_option_context_add_main_entries (optctx, options, NULL);

  g_autoptr(GError) error = NULL;
  if (!g_option_context_parse (optctx, &argc, &argv, &error)) {
    g_printerr ("Error parsing options: %s", error->message);
    return 1;
  }

  g_autoptr(GArray) config_dirs = load_config_dirs ();
  g_autoptr(GArray) paths = g_array_new (FALSE, FALSE, sizeof(gchar *));
  g_array_set_clear_func (paths, free_pointer_at);

  gboolean success = TRUE;

  if (opt_verbose) {
    g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);
  }

  if (opt_stdin) {
    g_debug ("Reading from stdin.");
    success = read_paths_from_stdin (paths);
  }

  if (opt_rest != NULL) {
    g_debug ("Adding command-line paths.");
    g_array_append_vals (paths, opt_rest, g_strv_length (opt_rest));
  }

  for (gint i = 0; i < paths->len; i++) {
    g_autofree gchar *path = g_strdup (g_array_index (paths, gchar *, i));
    g_debug ("Current path: %s", path);

    if (opt_resolve_paths && strchr (path, '/') == NULL) {
      gchar *old_path = path;
      path = find_application (old_path);
      g_free (old_path);

      if (path == NULL) {
        success = FALSE;
        continue;
      }

      g_debug ("Resolved as %s.", path);
    }

    if (!process_file (config_dirs, path, opt_dry_run)) {
      success = FALSE;
    }
  }

  g_regex_unref (global_replace_re);
  return success ? 0 : 1;
}
