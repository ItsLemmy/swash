#include "window.h"
#include "window-private.h"
#include "swash-config.h"

#include "export.h"
#include "ocr.h"
#include "render.h"
#include "stroke.h"

#include <cairo.h>
#include <string.h>

G_DEFINE_FINAL_TYPE(SwashWindow, swash_window, ADW_TYPE_APPLICATION_WINDOW)

#define SWASH_SETTINGS_GROUP "preferences"
#define SWASH_SETTINGS_FILE "swash/settings.ini"
#define SWASH_WINDOW_STYLE_PROVIDER_PRIORITY (GTK_STYLE_PROVIDER_PRIORITY_USER + 1)
#define SWASH_RESOURCE_PREFIX "/dev/lemmy/swash"
#define SWASH_REPOSITORY_URL "https://github.com/ItsLemmy/swash"
#define SWASH_ISSUES_URL SWASH_REPOSITORY_URL "/issues"

static void swash_window_clear_ocr_results(SwashWindow *self);
static void swash_window_set_ocr_panel_visible(SwashWindow *self,
                                                  gboolean        visible);
static void swash_window_ocr_panel_open_changed(GObject    *object,
                                                   GParamSpec *pspec,
                                                   gpointer    user_data);
static void swash_window_show_error(SwashWindow *self,
                                       const char     *message);
static gboolean swash_window_has_unsaved_changes(SwashWindow *self);
static void swash_window_copy_export_ready(GObject      *source_object,
                                              GAsyncResult *result,
                                              gpointer      user_data);
static gboolean swash_window_parse_accelerator(const char       *accelerator,
                                                  guint            *keyval,
                                                  GdkModifierType  *modifiers);
static void swash_window_apply_copy_shortcut(SwashWindow *self,
                                                const char     *accelerator);
static void swash_window_update_shortcut_label(GtkShortcutLabel *label,
                                                  const char       *accelerator);
static const char *swash_window_angle_snap_modifier_label(GdkModifierType modifiers);
static void swash_window_highlighter_overlap_changed(AdwSwitchRow   *row,
                                                        GParamSpec     *pspec,
                                                        SwashWindow *self);
static void swash_window_update_window_controls(SwashWindow *self);
static void swash_window_update_window_background(SwashWindow *self);
static void swash_window_update_widget_appearance(SwashWindow *self);
static void swash_window_save_preferences(SwashWindow *self);

#define SWASH_STATE_GROUP "state"

static const char *
swash_tool_key_name(SwashTool tool)
{
  switch (tool) {
  case SWASH_TOOL_PAN:       return "pan";
  case SWASH_TOOL_CROP:      return "crop";
  case SWASH_TOOL_BRUSH:     return "brush";
  case SWASH_TOOL_MARKER:    return "marker";
  case SWASH_TOOL_ERASER:    return "eraser";
  case SWASH_TOOL_RECTANGLE: return "rectangle";
  case SWASH_TOOL_CIRCLE:    return "circle";
  case SWASH_TOOL_LINE:      return "line";
  case SWASH_TOOL_ARROW:     return "arrow";
  case SWASH_TOOL_OCR:       return "ocr";
  case SWASH_TOOL_TEXT:      return "text";
  case SWASH_TOOL_BLUR:      return "blur";
  case SWASH_TOOL_NUMBERING: return "numbering";
  case SWASH_TOOL_MOVE:      return "move";
  default:                      return "brush";
  }
}

static SwashTool
swash_tool_from_key_name(const char *name)
{
  if (name == NULL)                         return SWASH_TOOL_BRUSH;
  if (g_strcmp0(name, "pan") == 0)          return SWASH_TOOL_PAN;
  if (g_strcmp0(name, "crop") == 0)         return SWASH_TOOL_CROP;
  if (g_strcmp0(name, "brush") == 0)        return SWASH_TOOL_BRUSH;
  if (g_strcmp0(name, "marker") == 0)       return SWASH_TOOL_MARKER;
  if (g_strcmp0(name, "eraser") == 0)       return SWASH_TOOL_ERASER;
  if (g_strcmp0(name, "rectangle") == 0)    return SWASH_TOOL_RECTANGLE;
  if (g_strcmp0(name, "circle") == 0)       return SWASH_TOOL_CIRCLE;
  if (g_strcmp0(name, "line") == 0)         return SWASH_TOOL_LINE;
  if (g_strcmp0(name, "arrow") == 0)        return SWASH_TOOL_ARROW;
  if (g_strcmp0(name, "ocr") == 0)          return SWASH_TOOL_OCR;
  if (g_strcmp0(name, "text") == 0)         return SWASH_TOOL_TEXT;
  if (g_strcmp0(name, "blur") == 0)         return SWASH_TOOL_BLUR;
  if (g_strcmp0(name, "numbering") == 0)   return SWASH_TOOL_NUMBERING;
  if (g_strcmp0(name, "move") == 0)         return SWASH_TOOL_MOVE;
  return SWASH_TOOL_BRUSH;
}

static void
swash_window_apply_default_tool_colors(SwashWindow *self)
{
  const GdkRGBA primary = { 0.96, 0.2, 0.28, 1.0 };
  const GdkRGBA highlighter = { 1.0, 0.91, 0.2, 1.0 };
  const GdkRGBA fill = { 0.96, 0.2, 0.28, 0.0 };

  for (int i = 0; i <= SWASH_TOOL_MOVE; i++) {
    self->tool_colors[i] = primary;
    self->tool_fill_colors[i] = fill;
  }

  self->tool_colors[SWASH_TOOL_MARKER] = highlighter;
  self->tool_colors[SWASH_TOOL_BLUR] = (GdkRGBA){0.0, 0.0, 0.0, 1.0};
  self->tool_colors[SWASH_TOOL_ERASER] = (GdkRGBA){1.0, 1.0, 1.0, 1.0};
}

static const char *
swash_window_background_mode_label(SwashWindowBackgroundMode mode)
{
  switch (mode) {
  case SWASH_WINDOW_BACKGROUND_FOLLOW_SYSTEM:
    return "Follow system theme";
  case SWASH_WINDOW_BACKGROUND_OPAQUE:
    return "Opaque";
  case SWASH_WINDOW_BACKGROUND_TRANSPARENT:
    return "Transparent";
  default:
    return "Follow system theme";
  }
}

static char *
swash_window_preferences_path(void)
{
  return g_build_filename(g_get_user_config_dir(), SWASH_SETTINGS_FILE, NULL);
}

static gboolean
swash_window_parse_accelerator(const char      *accelerator,
                                  guint           *keyval,
                                  GdkModifierType *modifiers)
{
  guint parsed_keyval = 0;
  GdkModifierType parsed_modifiers = 0;

  if (accelerator == NULL || *accelerator == '\0')
    return FALSE;

  gtk_accelerator_parse(accelerator, &parsed_keyval, &parsed_modifiers);
  if (parsed_keyval == 0)
    return FALSE;

  if (keyval != NULL)
    *keyval = gdk_keyval_to_lower(parsed_keyval);
  if (modifiers != NULL)
    *modifiers = parsed_modifiers & gtk_accelerator_get_default_mod_mask();

  return TRUE;
}

static void
swash_window_apply_copy_shortcut(SwashWindow *self,
                                    const char     *accelerator)
{
  g_clear_pointer(&self->copy_shortcut_accel, g_free);
  self->copy_shortcut_accel = g_strdup(accelerator);
}

static void
swash_window_update_shortcut_label(GtkShortcutLabel *label,
                                      const char       *accelerator)
{
  gtk_shortcut_label_set_accelerator(label,
                                     accelerator != NULL ? accelerator : "");
}

static const char *
swash_window_angle_snap_modifier_label(GdkModifierType modifiers)
{
  switch (modifiers & gtk_accelerator_get_default_mod_mask()) {
  case 0:
    return "Disabled";
  case GDK_SHIFT_MASK:
    return "Shift";
  case GDK_CONTROL_MASK:
    return "Ctrl";
  case GDK_ALT_MASK:
    return "Alt";
  case GDK_SUPER_MASK:
    return "Super";
  default:
    return "Shift";
  }
}

static void
swash_window_load_preferences(SwashWindow *self)
{
  g_autofree char *path = swash_window_preferences_path();
  g_autoptr(GKeyFile) key_file = g_key_file_new();
  g_autoptr(GError) error = NULL;

  if (!g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, &error)) {
    if (!g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
      g_warning("Failed to load preferences from %s: %s", path, error->message);
    return;
  }

  if (g_key_file_has_key(key_file, SWASH_SETTINGS_GROUP, "eraser_style", NULL)) {
    const int eraser_style = g_key_file_get_integer(key_file,
                                                    SWASH_SETTINGS_GROUP,
                                                    "eraser_style",
                                                    NULL);

    if (eraser_style >= SWASH_ERASER_STYLE_DUAL_RING
        && eraser_style <= SWASH_ERASER_STYLE_PATTERN)
      self->eraser_style = eraser_style;
  }

  if (g_key_file_has_key(key_file, SWASH_SETTINGS_GROUP, "window_background_mode", NULL)) {
    const int mode = g_key_file_get_integer(key_file,
                                            SWASH_SETTINGS_GROUP,
                                            "window_background_mode",
                                            NULL);

    if (mode >= SWASH_WINDOW_BACKGROUND_FOLLOW_SYSTEM
        && mode <= SWASH_WINDOW_BACKGROUND_TRANSPARENT)
      self->window_background_mode = mode;
  } else if (g_key_file_has_key(key_file,
                                SWASH_SETTINGS_GROUP,
                                "window_transparency_enabled",
                                NULL)) {
    self->window_background_mode = g_key_file_get_boolean(key_file,
                                                          SWASH_SETTINGS_GROUP,
                                                          "window_transparency_enabled",
                                                          NULL)
                                 ? SWASH_WINDOW_BACKGROUND_TRANSPARENT
                                 : SWASH_WINDOW_BACKGROUND_OPAQUE;
  }

  if (g_key_file_has_key(key_file, SWASH_SETTINGS_GROUP, "window_background_opacity", NULL)) {
    const double opacity = g_key_file_get_double(key_file,
                                                 SWASH_SETTINGS_GROUP,
                                                 "window_background_opacity",
                                                 NULL);

    if (opacity >= 0.1 && opacity <= 1.0)
      self->window_background_opacity = opacity;
  }

  if (g_key_file_has_key(key_file, SWASH_SETTINGS_GROUP, "floating_controls_blur", NULL))
    self->floating_controls_blur = g_key_file_get_boolean(key_file,
                                                          SWASH_SETTINGS_GROUP,
                                                          "floating_controls_blur",
                                                          NULL);

  if (g_key_file_has_key(key_file, SWASH_SETTINGS_GROUP, "floating_controls_opacity", NULL)) {
    const double opacity = g_key_file_get_double(key_file,
                                                 SWASH_SETTINGS_GROUP,
                                                 "floating_controls_opacity",
                                                 NULL);

    if (opacity >= 0.0 && opacity <= 1.0)
      self->floating_controls_opacity = opacity;
  }

  if (g_key_file_has_key(key_file, SWASH_SETTINGS_GROUP, "esc_closes_window", NULL))
    self->esc_closes_window = g_key_file_get_boolean(key_file,
                                                     SWASH_SETTINGS_GROUP,
                                                     "esc_closes_window",
                                                     NULL);

  if (g_key_file_has_key(key_file, SWASH_SETTINGS_GROUP, "copy_shortcut_enabled", NULL))
    self->copy_shortcut_enabled = g_key_file_get_boolean(key_file,
                                                         SWASH_SETTINGS_GROUP,
                                                         "copy_shortcut_enabled",
                                                         NULL);

  if (g_key_file_has_key(key_file, SWASH_SETTINGS_GROUP, "auto_copy_latest_change", NULL))
    self->auto_copy_latest_change = g_key_file_get_boolean(key_file,
                                                           SWASH_SETTINGS_GROUP,
                                                           "auto_copy_latest_change",
                                                           NULL);

  if (g_key_file_has_key(key_file, SWASH_SETTINGS_GROUP, "allow_highlighter_overlap", NULL))
    self->allow_highlighter_overlap = g_key_file_get_boolean(key_file,
                                                             SWASH_SETTINGS_GROUP,
                                                             "allow_highlighter_overlap",
                                                             NULL);

  if (g_key_file_has_key(key_file, SWASH_SETTINGS_GROUP, "copy_shortcut", NULL)) {
    g_autofree char *accelerator = g_key_file_get_string(key_file,
                                                         SWASH_SETTINGS_GROUP,
                                                         "copy_shortcut",
                                                         NULL);

    if (swash_window_parse_accelerator(accelerator, NULL, NULL))
      swash_window_apply_copy_shortcut(self, accelerator);
  }

  if (g_key_file_has_key(key_file, SWASH_SETTINGS_GROUP, "angle_snap_modifiers", NULL)) {
    const int modifiers = g_key_file_get_integer(key_file,
                                                 SWASH_SETTINGS_GROUP,
                                                 "angle_snap_modifiers",
                                                 NULL);

    switch (modifiers & gtk_accelerator_get_default_mod_mask()) {
    case 0:
    case GDK_SHIFT_MASK:
    case GDK_CONTROL_MASK:
    case GDK_ALT_MASK:
    case GDK_SUPER_MASK:
      self->angle_snap_modifiers = modifiers & gtk_accelerator_get_default_mod_mask();
      break;
    default:
      break;
    }
  }

}

static void
swash_window_load_state(SwashWindow *self)
{
  g_autofree char *path = swash_window_preferences_path();
  g_autoptr(GKeyFile) key_file = g_key_file_new();

  if (!g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, NULL))
    return;

  if (!g_key_file_has_group(key_file, SWASH_STATE_GROUP))
    return;

  if (g_key_file_has_key(key_file, SWASH_STATE_GROUP, "active_tool", NULL)) {
    g_autofree char *name = g_key_file_get_string(key_file, SWASH_STATE_GROUP, "active_tool", NULL);

    self->active_tool = swash_tool_from_key_name(name);
  }

  if (g_key_file_has_key(key_file, SWASH_STATE_GROUP, "blur_type", NULL)) {
    const int bt = g_key_file_get_integer(key_file, SWASH_STATE_GROUP, "blur_type", NULL);

    if (bt == 0 || bt == 1)
      self->blur_type = bt;
  }

  for (int i = 0; i <= SWASH_TOOL_MOVE; i++) {
    const char *name = swash_tool_key_name(i);
    g_autofree char *width_key = g_strdup_printf("tool_width_%s", name);
    g_autofree char *color_key = g_strdup_printf("tool_color_%s", name);
    g_autofree char *fill_key = g_strdup_printf("tool_fill_color_%s", name);

    if (g_key_file_has_key(key_file, SWASH_STATE_GROUP, width_key, NULL)) {
      const double w = g_key_file_get_double(key_file, SWASH_STATE_GROUP, width_key, NULL);

      if (w >= 1.0 && w <= 200.0)
        self->tool_widths[i] = w;
    }

    if (g_key_file_has_key(key_file, SWASH_STATE_GROUP, color_key, NULL)) {
      g_autofree char *val = g_key_file_get_string(key_file, SWASH_STATE_GROUP, color_key, NULL);

      if (val != NULL)
        gdk_rgba_parse(&self->tool_colors[i], val);
    }

    if (g_key_file_has_key(key_file, SWASH_STATE_GROUP, fill_key, NULL)) {
      g_autofree char *val = g_key_file_get_string(key_file, SWASH_STATE_GROUP, fill_key, NULL);

      if (val != NULL)
        gdk_rgba_parse(&self->tool_fill_colors[i], val);
    }
  }
}

static void
swash_window_save_preferences(SwashWindow *self)
{
  g_autofree char *path = swash_window_preferences_path();
  g_autofree char *directory = g_path_get_dirname(path);
  g_autoptr(GKeyFile) key_file = g_key_file_new();
  g_autofree char *data = NULL;
  gsize data_length = 0;
  g_autoptr(GError) error = NULL;

  g_key_file_set_integer(key_file,
                         SWASH_SETTINGS_GROUP,
                         "eraser_style",
                         self->eraser_style);
  g_key_file_set_integer(key_file,
                         SWASH_SETTINGS_GROUP,
                         "window_background_mode",
                         self->window_background_mode);
  g_key_file_set_double(key_file,
                        SWASH_SETTINGS_GROUP,
                        "window_background_opacity",
                        self->window_background_opacity);
  g_key_file_set_boolean(key_file,
                         SWASH_SETTINGS_GROUP,
                         "floating_controls_blur",
                         self->floating_controls_blur);
  g_key_file_set_double(key_file,
                        SWASH_SETTINGS_GROUP,
                        "floating_controls_opacity",
                        self->floating_controls_opacity);
  g_key_file_set_boolean(key_file,
                         SWASH_SETTINGS_GROUP,
                         "esc_closes_window",
                         self->esc_closes_window);
  g_key_file_set_boolean(key_file,
                         SWASH_SETTINGS_GROUP,
                         "copy_shortcut_enabled",
                         self->copy_shortcut_enabled);
  g_key_file_set_boolean(key_file,
                         SWASH_SETTINGS_GROUP,
                         "auto_copy_latest_change",
                         self->auto_copy_latest_change);
  g_key_file_set_boolean(key_file,
                         SWASH_SETTINGS_GROUP,
                         "allow_highlighter_overlap",
                         self->allow_highlighter_overlap);
  g_key_file_set_string(key_file,
                        SWASH_SETTINGS_GROUP,
                        "copy_shortcut",
                        self->copy_shortcut_accel);
  g_key_file_set_integer(key_file,
                         SWASH_SETTINGS_GROUP,
                         "angle_snap_modifiers",
                         self->angle_snap_modifiers);
  g_key_file_set_string(key_file,
                        SWASH_STATE_GROUP,
                        "active_tool",
                        swash_tool_key_name(self->active_tool));
  g_key_file_set_integer(key_file,
                         SWASH_STATE_GROUP,
                         "blur_type",
                         self->blur_type);

  for (int i = 0; i <= SWASH_TOOL_MOVE; i++) {
    const char *name = swash_tool_key_name(i);
    g_autofree char *width_key = g_strdup_printf("tool_width_%s", name);
    g_autofree char *color_key = g_strdup_printf("tool_color_%s", name);
    g_autofree char *fill_key = g_strdup_printf("tool_fill_color_%s", name);
    g_autofree char *color_str = gdk_rgba_to_string(&self->tool_colors[i]);
    g_autofree char *fill_str = gdk_rgba_to_string(&self->tool_fill_colors[i]);

    g_key_file_set_double(key_file, SWASH_STATE_GROUP, width_key, self->tool_widths[i]);
    g_key_file_set_string(key_file, SWASH_STATE_GROUP, color_key, color_str);
    g_key_file_set_string(key_file, SWASH_STATE_GROUP, fill_key, fill_str);
  }

  if (g_mkdir_with_parents(directory, 0700) != 0) {
    g_warning("Failed to create preferences directory %s", directory);
    return;
  }

  data = g_key_file_to_data(key_file, &data_length, NULL);
  if (!g_file_set_contents(path, data, data_length, &error))
    g_warning("Failed to save preferences to %s: %s", path, error->message);
}

static const char *
swash_window_eraser_style_label(SwashEraserStyle style)
{
  switch (style) {
  case SWASH_ERASER_STYLE_DUAL_RING:
    return "Dual ring";
  case SWASH_ERASER_STYLE_DASHED_RING:
    return "Dashed ring";
  case SWASH_ERASER_STYLE_PATTERN:
    return "Pattern fill";
  default:
    return "Dual ring";
  }
}

static void
swash_window_eraser_style_changed(AdwComboRow    *row,
                                     GParamSpec     *pspec,
                                     SwashWindow *self)
{
  (void) pspec;

  self->eraser_style = adw_combo_row_get_selected(row);
  swash_window_save_preferences(self);
  gtk_widget_queue_draw(GTK_WIDGET(self->drawing_area));
}

static void
swash_window_window_controls_changed(GListModel     *model,
                                        guint           position,
                                        guint           removed,
                                        guint           added,
                                        SwashWindow *self)
{
  (void) model;
  (void) position;
  (void) removed;
  (void) added;

  swash_window_update_window_controls(self);
}

static void
swash_window_update_window_controls(SwashWindow *self)
{
  const gboolean has_start_controls = self->start_window_controls_children != NULL
                                   && g_list_model_get_n_items(self->start_window_controls_children) > 0;
  const gboolean has_end_controls = self->end_window_controls_children != NULL
                                 && g_list_model_get_n_items(self->end_window_controls_children) > 0;

  gtk_widget_set_visible(GTK_WIDGET(self->start_window_controls), has_start_controls);
  gtk_widget_set_visible(self->start_window_controls_pill, has_start_controls);
  gtk_widget_set_visible(GTK_WIDGET(self->end_window_controls), has_end_controls);
}

static void
swash_window_background_mode_changed(AdwComboRow    *row,
                                        GParamSpec     *pspec,
                                        SwashWindow *self)
{
  GtkWidget *opacity_row;

  (void) pspec;

  self->window_background_mode = adw_combo_row_get_selected(row);
  opacity_row = g_object_get_data(G_OBJECT(row), "opacity-row");
  if (opacity_row != NULL)
    gtk_widget_set_sensitive(opacity_row,
                             self->window_background_mode == SWASH_WINDOW_BACKGROUND_TRANSPARENT);
  swash_window_save_preferences(self);
  swash_window_update_window_background(self);
}

static void
swash_window_transparency_opacity_changed(GtkSpinButton  *spin_button,
                                             SwashWindow *self)
{
  self->window_background_opacity = gtk_spin_button_get_value(spin_button);
  swash_window_save_preferences(self);
  swash_window_update_window_background(self);
}

static void
swash_window_floating_controls_blur_changed(AdwSwitchRow   *row,
                                               GParamSpec     *pspec,
                                               SwashWindow *self)
{
  (void) pspec;

  self->floating_controls_blur = adw_switch_row_get_active(row);
  swash_window_save_preferences(self);
  swash_window_update_widget_appearance(self);
}

static void
swash_window_floating_controls_opacity_changed(GtkSpinButton  *spin_button,
                                                  SwashWindow *self)
{
  self->floating_controls_opacity = gtk_spin_button_get_value(spin_button);
  swash_window_save_preferences(self);
  swash_window_update_widget_appearance(self);
}

static void
swash_window_esc_closes_window_changed(AdwSwitchRow   *row,
                                          GParamSpec     *pspec,
                                          SwashWindow *self)
{
  (void) pspec;

  self->esc_closes_window = adw_switch_row_get_active(row);
  swash_window_save_preferences(self);
}

static void
swash_window_copy_shortcut_enabled_changed(AdwSwitchRow   *row,
                                              GParamSpec     *pspec,
                                              SwashWindow *self)
{
  GtkWidget *shortcut_row;

  (void) pspec;

  self->copy_shortcut_enabled = adw_switch_row_get_active(row);
  shortcut_row = g_object_get_data(G_OBJECT(row), "shortcut-row");
  if (shortcut_row != NULL)
    gtk_widget_set_sensitive(shortcut_row, self->copy_shortcut_enabled);
  swash_window_save_preferences(self);
}

static void
swash_window_auto_copy_latest_change_changed(AdwSwitchRow   *row,
                                                GParamSpec     *pspec,
                                                SwashWindow *self)
{
  (void) pspec;

  self->auto_copy_latest_change = adw_switch_row_get_active(row);
  swash_window_save_preferences(self);
}

static void
swash_window_highlighter_overlap_changed(AdwSwitchRow   *row,
                                            GParamSpec     *pspec,
                                            SwashWindow *self)
{
  (void) pspec;

  self->allow_highlighter_overlap = adw_switch_row_get_active(row);
  gtk_widget_queue_draw(GTK_WIDGET(self->drawing_area));
  swash_window_save_preferences(self);
}

static void
swash_window_angle_snap_modifier_changed(AdwComboRow    *row,
                                            GParamSpec     *pspec,
                                            SwashWindow *self)
{
  static const GdkModifierType snap_modifiers[] = {
    0,
    GDK_SHIFT_MASK,
    GDK_CONTROL_MASK,
    GDK_ALT_MASK,
    GDK_SUPER_MASK,
  };
  const guint selected = adw_combo_row_get_selected(row);

  (void) pspec;

  if (selected >= G_N_ELEMENTS(snap_modifiers))
    return;

  self->angle_snap_modifiers = snap_modifiers[selected];
  swash_window_save_preferences(self);
}

static void
swash_window_apply_copy_shortcut_row(GtkButton *button,
                                        gpointer   user_data)
{
  GtkWidget *shortcut_label;
  (void) user_data;

  gtk_button_set_has_frame(button, TRUE);
  gtk_widget_add_css_class(GTK_WIDGET(button), "suggested-action");
  shortcut_label = g_object_get_data(G_OBJECT(button), "shortcut-label");
  if (shortcut_label != NULL)
    gtk_widget_set_sensitive(shortcut_label, FALSE);
  g_object_set_data(G_OBJECT(button), "capturing", GINT_TO_POINTER(TRUE));
}

static gboolean
swash_window_copy_shortcut_capture_key_pressed(GtkEventControllerKey *controller,
                                                  guint                  keyval,
                                                  guint                  keycode,
                                                  GdkModifierType        state,
                                                  gpointer               user_data)
{
  GtkWidget *button = GTK_WIDGET(user_data);
  SwashWindow *self = SWASH_WINDOW(g_object_get_data(G_OBJECT(button), "window"));
  GtkShortcutLabel *shortcut_label = GTK_SHORTCUT_LABEL(g_object_get_data(G_OBJECT(button), "shortcut-label"));
  GdkModifierType modifiers;
  g_autofree char *accelerator = NULL;

  (void) controller;
  (void) keycode;

  if (!GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "capturing")))
    return FALSE;

  modifiers = state & gtk_accelerator_get_default_mod_mask();

  if (keyval == GDK_KEY_Escape && modifiers == 0) {
    gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
    gtk_widget_remove_css_class(button, "suggested-action");
    gtk_widget_set_sensitive(GTK_WIDGET(shortcut_label), TRUE);
    g_object_set_data(G_OBJECT(button), "capturing", GINT_TO_POINTER(FALSE));
    return TRUE;
  }

  if (keyval == GDK_KEY_BackSpace || keyval == GDK_KEY_Delete) {
    swash_window_apply_copy_shortcut(self, "");
    swash_window_update_shortcut_label(shortcut_label, "");
  } else {
    if (keyval == GDK_KEY_Shift_L || keyval == GDK_KEY_Shift_R
        || keyval == GDK_KEY_Control_L || keyval == GDK_KEY_Control_R
        || keyval == GDK_KEY_Alt_L || keyval == GDK_KEY_Alt_R
        || keyval == GDK_KEY_Meta_L || keyval == GDK_KEY_Meta_R
        || keyval == GDK_KEY_Super_L || keyval == GDK_KEY_Super_R
        || keyval == GDK_KEY_Hyper_L || keyval == GDK_KEY_Hyper_R)
      return TRUE;

    accelerator = gtk_accelerator_name(keyval, modifiers);
    if (!swash_window_parse_accelerator(accelerator, NULL, NULL))
      return TRUE;

    swash_window_apply_copy_shortcut(self, accelerator);
    swash_window_update_shortcut_label(shortcut_label, self->copy_shortcut_accel);
  }

  swash_window_save_preferences(self);
  gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
  gtk_widget_remove_css_class(button, "suggested-action");
  gtk_widget_set_sensitive(GTK_WIDGET(shortcut_label), TRUE);
  g_object_set_data(G_OBJECT(button), "capturing", GINT_TO_POINTER(FALSE));

  return TRUE;
}

static void
swash_window_update_window_background(SwashWindow *self)
{
  g_autofree char *css = NULL;

  if (self->window_css_provider == NULL)
    return;

  if (self->window_background_mode == SWASH_WINDOW_BACKGROUND_FOLLOW_SYSTEM) {
    gtk_css_provider_load_from_string(self->window_css_provider, "");
    return;
  }

  //dont u just love how gtk css requires repeating the same properties for some reason
  if (self->window_background_mode == SWASH_WINDOW_BACKGROUND_OPAQUE) {
    gtk_css_provider_load_from_string(self->window_css_provider,
                                      "window.window { background: @window_bg_color; background-color: @window_bg_color; }");
    return;
  }

  css = g_strdup_printf("window.window { background: alpha(@window_bg_color, %.1f); background-color: alpha(@window_bg_color, %.1f); }",
                        self->window_background_opacity,
                        self->window_background_opacity);
  gtk_css_provider_load_from_string(self->window_css_provider, css);
}

static void
swash_window_update_widget_appearance(SwashWindow *self)
{
  g_autofree char *css = NULL;
  const char *blur = self->floating_controls_blur ? "blur(18px)" : "none";

  if (self->widget_css_provider == NULL)
    return;

  css = g_strdup_printf(
    ".overlay-pill, bottom-sheet > sheet.background {"
    " color: white;"
    " background: alpha(black, %.2f);"
    " background-color: alpha(black, %.2f);"
    " border: 1px solid alpha(white, 0.12);"
    " border-radius: 14px;"
    " backdrop-filter: %s;"
    " box-shadow: 0 10px 32px alpha(black, 0.18);"
    " background-image: none;"
    "}"
    ".app-chrome-group { border-color: alpha(white, 0.16); }",
    self->floating_controls_opacity,
    self->floating_controls_opacity,
    blur);
  gtk_css_provider_load_from_string(self->widget_css_provider, css);
}

static void
swash_window_show_preferences(SwashWindow *self)
{
  AdwPreferencesDialog *dialog;
  AdwPreferencesPage *page;
  AdwPreferencesGroup *group;
  AdwPreferencesGroup *appearance_group;
  AdwPreferencesGroup *shortcuts_group;
  AdwComboRow *row;
  AdwComboRow *background_mode_row;
  AdwSwitchRow *floating_controls_blur_row;
  AdwSwitchRow *esc_closes_window_row;
  AdwSwitchRow *copy_shortcut_enabled_row;
  AdwSwitchRow *auto_copy_latest_change_row;
  AdwSwitchRow *highlighter_overlap_row;
  AdwComboRow *angle_snap_modifier_row;
  AdwActionRow *opacity_row;
  AdwActionRow *floating_controls_opacity_row;
  AdwActionRow *copy_shortcut_row;
  GtkStringList *model;
  GtkStringList *background_model;
  GtkStringList *angle_snap_model;
  GtkAdjustment *opacity_adjustment;
  GtkSpinButton *opacity_spin_button;
  GtkAdjustment *floating_controls_opacity_adjustment;
  GtkSpinButton *floating_controls_opacity_spin_button;
  GtkWidget *copy_shortcut_label;
  GtkWidget *copy_shortcut_button;
  GtkWidget *copy_shortcut_button_box;
  GtkEventController *copy_shortcut_key_controller;

  dialog = ADW_PREFERENCES_DIALOG(adw_preferences_dialog_new());
  page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
  group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
  appearance_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
  shortcuts_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
  row = ADW_COMBO_ROW(adw_combo_row_new());
  background_mode_row = ADW_COMBO_ROW(adw_combo_row_new());
  floating_controls_blur_row = ADW_SWITCH_ROW(adw_switch_row_new());
  esc_closes_window_row = ADW_SWITCH_ROW(adw_switch_row_new());
  copy_shortcut_enabled_row = ADW_SWITCH_ROW(adw_switch_row_new());
  auto_copy_latest_change_row = ADW_SWITCH_ROW(adw_switch_row_new());
  highlighter_overlap_row = ADW_SWITCH_ROW(adw_switch_row_new());
  angle_snap_modifier_row = ADW_COMBO_ROW(adw_combo_row_new());
  opacity_row = ADW_ACTION_ROW(adw_action_row_new());
  floating_controls_opacity_row = ADW_ACTION_ROW(adw_action_row_new());
  copy_shortcut_row = ADW_ACTION_ROW(adw_action_row_new());
  opacity_adjustment = gtk_adjustment_new(self->window_background_opacity, 0.1, 1.0, 0.1, 0.1, 0.0);
  opacity_spin_button = GTK_SPIN_BUTTON(gtk_spin_button_new(opacity_adjustment, 0.1, 1));
  floating_controls_opacity_adjustment = gtk_adjustment_new(self->floating_controls_opacity, 0.0, 1.0, 0.1, 0.1, 0.0);
  floating_controls_opacity_spin_button = GTK_SPIN_BUTTON(gtk_spin_button_new(floating_controls_opacity_adjustment, 0.1, 1));
  copy_shortcut_label = gtk_shortcut_label_new(NULL);
  copy_shortcut_button = gtk_button_new();
  copy_shortcut_button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  copy_shortcut_key_controller = gtk_event_controller_key_new();
  model = gtk_string_list_new((const char *[]) {
    swash_window_eraser_style_label(SWASH_ERASER_STYLE_DUAL_RING),
    swash_window_eraser_style_label(SWASH_ERASER_STYLE_DASHED_RING),
    swash_window_eraser_style_label(SWASH_ERASER_STYLE_PATTERN),
    NULL,
  });
  //yes this is really how you have to do dropdowns in gtk
  background_model = gtk_string_list_new((const char *[]) {
    swash_window_background_mode_label(SWASH_WINDOW_BACKGROUND_FOLLOW_SYSTEM),
    swash_window_background_mode_label(SWASH_WINDOW_BACKGROUND_OPAQUE),
    swash_window_background_mode_label(SWASH_WINDOW_BACKGROUND_TRANSPARENT),
    NULL,
  });
  angle_snap_model = gtk_string_list_new((const char *[]) {
    swash_window_angle_snap_modifier_label(0),
    swash_window_angle_snap_modifier_label(GDK_SHIFT_MASK),
    swash_window_angle_snap_modifier_label(GDK_CONTROL_MASK),
    swash_window_angle_snap_modifier_label(GDK_ALT_MASK),
    swash_window_angle_snap_modifier_label(GDK_SUPER_MASK),
    NULL,
  });

  adw_preferences_page_set_title(page, "General");
  adw_preferences_group_set_title(group, "General");
  adw_preferences_group_set_title(appearance_group, "Appearance");
  adw_preferences_group_set_title(shortcuts_group, "Shortcuts");
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), "Eraser Styling");
  adw_combo_row_set_model(row, G_LIST_MODEL(model));
  adw_combo_row_set_selected(row, self->eraser_style);
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(background_mode_row), "Window background");
  adw_combo_row_set_model(background_mode_row, G_LIST_MODEL(background_model));
  adw_combo_row_set_selected(background_mode_row, self->window_background_mode);
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(opacity_row), "Window background opacity");
  gtk_spin_button_set_numeric(opacity_spin_button, TRUE);
  gtk_widget_set_valign(GTK_WIDGET(opacity_spin_button), GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(GTK_WIDGET(opacity_spin_button), 88, -1);
  adw_action_row_add_suffix(opacity_row, GTK_WIDGET(opacity_spin_button));
  adw_action_row_set_activatable_widget(opacity_row, GTK_WIDGET(opacity_spin_button));
  gtk_widget_set_sensitive(GTK_WIDGET(opacity_row),
                           self->window_background_mode == SWASH_WINDOW_BACKGROUND_TRANSPARENT);
  g_object_set_data(G_OBJECT(background_mode_row), "opacity-row", opacity_row);
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(floating_controls_blur_row), "Blur the background of controls");
  adw_switch_row_set_active(floating_controls_blur_row, self->floating_controls_blur);
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(floating_controls_opacity_row), "Controls background opacity");
  gtk_spin_button_set_numeric(floating_controls_opacity_spin_button, TRUE);
  gtk_widget_set_valign(GTK_WIDGET(floating_controls_opacity_spin_button), GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(GTK_WIDGET(floating_controls_opacity_spin_button), 88, -1);
  adw_action_row_add_suffix(floating_controls_opacity_row, GTK_WIDGET(floating_controls_opacity_spin_button));
  adw_action_row_set_activatable_widget(floating_controls_opacity_row,
                                        GTK_WIDGET(floating_controls_opacity_spin_button));
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(esc_closes_window_row), "Escape closes window");
  adw_switch_row_set_active(esc_closes_window_row, self->esc_closes_window);
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(copy_shortcut_enabled_row), "Enable copy shortcut");
  adw_switch_row_set_active(copy_shortcut_enabled_row, self->copy_shortcut_enabled);
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(copy_shortcut_row), "Copy shortcut");
  swash_window_update_shortcut_label(GTK_SHORTCUT_LABEL(copy_shortcut_label), self->copy_shortcut_accel);
  gtk_widget_set_valign(copy_shortcut_label, GTK_ALIGN_CENTER);
  gtk_widget_set_halign(copy_shortcut_label, GTK_ALIGN_END);
  gtk_widget_set_hexpand(copy_shortcut_button_box, FALSE);
  gtk_widget_set_halign(copy_shortcut_button_box, GTK_ALIGN_END);
  gtk_box_append(GTK_BOX(copy_shortcut_button_box), copy_shortcut_label);
  gtk_button_set_child(GTK_BUTTON(copy_shortcut_button), copy_shortcut_button_box);
  gtk_widget_set_valign(copy_shortcut_button, GTK_ALIGN_CENTER);
  gtk_widget_add_controller(copy_shortcut_button, copy_shortcut_key_controller);
  adw_action_row_add_suffix(copy_shortcut_row, copy_shortcut_button);
  gtk_widget_set_sensitive(GTK_WIDGET(copy_shortcut_row), self->copy_shortcut_enabled);
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(angle_snap_modifier_row), "Snap modifier");
  adw_combo_row_set_model(angle_snap_modifier_row, G_LIST_MODEL(angle_snap_model));
  switch (self->angle_snap_modifiers) {
  case 0:
    adw_combo_row_set_selected(angle_snap_modifier_row, 0);
    break;
  case GDK_CONTROL_MASK:
    adw_combo_row_set_selected(angle_snap_modifier_row, 2);
    break;
  case GDK_ALT_MASK:
    adw_combo_row_set_selected(angle_snap_modifier_row, 3);
    break;
  case GDK_SUPER_MASK:
    adw_combo_row_set_selected(angle_snap_modifier_row, 4);
    break;
  case GDK_SHIFT_MASK:
  default:
    adw_combo_row_set_selected(angle_snap_modifier_row, 1);
    break;
  }
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(auto_copy_latest_change_row), "Auto-copy latest change");
  adw_switch_row_set_active(auto_copy_latest_change_row, self->auto_copy_latest_change);
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(highlighter_overlap_row), "Allow highlighter strokes to overlap");
  adw_switch_row_set_active(highlighter_overlap_row, self->allow_highlighter_overlap);
  adw_preferences_group_add(group, GTK_WIDGET(row));
  adw_preferences_group_add(group, GTK_WIDGET(highlighter_overlap_row));
  adw_preferences_group_add(group, GTK_WIDGET(auto_copy_latest_change_row));
  adw_preferences_group_add(appearance_group, GTK_WIDGET(background_mode_row));
  adw_preferences_group_add(appearance_group, GTK_WIDGET(opacity_row));
  adw_preferences_group_add(appearance_group, GTK_WIDGET(floating_controls_blur_row));
  adw_preferences_group_add(appearance_group, GTK_WIDGET(floating_controls_opacity_row));
  adw_preferences_group_add(shortcuts_group, GTK_WIDGET(esc_closes_window_row));
  adw_preferences_group_add(shortcuts_group, GTK_WIDGET(copy_shortcut_enabled_row));
  adw_preferences_group_add(shortcuts_group, GTK_WIDGET(copy_shortcut_row));
  adw_preferences_group_add(shortcuts_group, GTK_WIDGET(angle_snap_modifier_row));
  adw_preferences_page_add(page, group);
  adw_preferences_page_add(page, appearance_group);
  adw_preferences_page_add(page, shortcuts_group);
  adw_preferences_dialog_add(dialog, page);
  adw_dialog_set_title(ADW_DIALOG(dialog), "Preferences");
  adw_preferences_dialog_set_search_enabled(dialog, FALSE);
  adw_dialog_set_content_width(ADW_DIALOG(dialog), 420);

  g_signal_connect(row, "notify::selected", G_CALLBACK(swash_window_eraser_style_changed), self);
  g_signal_connect(background_mode_row,
                   "notify::selected",
                   G_CALLBACK(swash_window_background_mode_changed),
                   self);
  g_signal_connect(opacity_spin_button,
                   "value-changed",
                   G_CALLBACK(swash_window_transparency_opacity_changed),
                   self);
  g_signal_connect(floating_controls_blur_row,
                   "notify::active",
                   G_CALLBACK(swash_window_floating_controls_blur_changed),
                   self);
  g_signal_connect(floating_controls_opacity_spin_button,
                   "value-changed",
                   G_CALLBACK(swash_window_floating_controls_opacity_changed),
                   self);
  g_signal_connect(esc_closes_window_row,
                   "notify::active",
                   G_CALLBACK(swash_window_esc_closes_window_changed),
                   self);
  g_signal_connect(copy_shortcut_enabled_row,
                   "notify::active",
                   G_CALLBACK(swash_window_copy_shortcut_enabled_changed),
                   self);
  g_signal_connect(angle_snap_modifier_row,
                   "notify::selected",
                   G_CALLBACK(swash_window_angle_snap_modifier_changed),
                   self);
  g_signal_connect(auto_copy_latest_change_row,
                   "notify::active",
                   G_CALLBACK(swash_window_auto_copy_latest_change_changed),
                   self);
  g_signal_connect(highlighter_overlap_row,
                   "notify::active",
                   G_CALLBACK(swash_window_highlighter_overlap_changed),
                   self);
  g_object_set_data(G_OBJECT(copy_shortcut_enabled_row), "shortcut-row", copy_shortcut_row);
  g_object_set_data(G_OBJECT(copy_shortcut_button), "window", self);
  g_object_set_data(G_OBJECT(copy_shortcut_button), "shortcut-label", copy_shortcut_label);
  g_signal_connect(copy_shortcut_button,
                   "clicked",
                   G_CALLBACK(swash_window_apply_copy_shortcut_row),
                   NULL);
  g_signal_connect(copy_shortcut_key_controller,
                   "key-pressed",
                   G_CALLBACK(swash_window_copy_shortcut_capture_key_pressed),
                   copy_shortcut_button);

  adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(self));
  g_object_unref(model);
  g_object_unref(background_model);
  g_object_unref(angle_snap_model);
}

static void
swash_window_show_about(SwashWindow *self)
{
  AdwDialog *dialog = adw_about_dialog_new();
  AdwAboutDialog *about = ADW_ABOUT_DIALOG(dialog);
  g_autofree char *debug_info = NULL;

  debug_info = g_strdup_printf("Swash %s\nGTK %u.%u.%u\nlibadwaita %u.%u.%u",
                               SWASH_VERSION,
                               gtk_get_major_version(),
                               gtk_get_minor_version(),
                               gtk_get_micro_version(),
                               adw_get_major_version(),
                               adw_get_minor_version(),
                               adw_get_micro_version());

  g_object_set(about,
               "application-name", "Swash",
               "application-icon", "dev.lemmy.swash",
               "comments", "A fast screenshot annotator and lightweight image editor for Linux.",
               "copyright", "© 2026 Lemmy",
               "debug-info", debug_info,
               "debug-info-filename", "swash-debug-info.txt",
               "developer-name", "Lemmy",
               "developers", (const char *[]) { "Lemmy", NULL },
               "issue-url", SWASH_ISSUES_URL,
               "license-type", GTK_LICENSE_GPL_3_0,
               "version", SWASH_VERSION,
               "website", SWASH_REPOSITORY_URL,
               NULL);

  adw_about_dialog_add_credit_section(
    about,
    "Based on",
    (const char *[]) { "Waytator by faetalize", NULL });

  adw_dialog_present(dialog, GTK_WIDGET(self));
}

static void
swash_window_preferences_action(GtkWidget  *widget,
                                   const char *action_name,
                                   GVariant   *parameter)
{
  (void) action_name;
  (void) parameter;

  swash_window_show_preferences(SWASH_WINDOW(widget));
}

static void
swash_window_about_action(GtkWidget  *widget,
                             const char *action_name,
                             GVariant   *parameter)
{
  (void) action_name;
  (void) parameter;

  swash_window_show_about(SWASH_WINDOW(widget));
}

GPtrArray *
swash_window_strokes(SwashWindow *self)
{
  return swash_document_get_strokes(self->document);
}

static void
swash_window_set_text_view_text(GtkTextView *view,
                                   const char  *text)
{
  GtkTextBuffer *buffer = gtk_text_view_get_buffer(view);

  gtk_text_buffer_set_text(buffer, text != NULL ? text : "", -1);
}

static char *
swash_window_build_ocr_all_text(GPtrArray *lines)
{
  GString *text;
  guint i;

  if (lines == NULL || lines->len == 0)
    return g_strdup("");

  text = g_string_new(NULL);

  for (i = 0; i < lines->len; i++) {
    SwashOcrLine *line = g_ptr_array_index(lines, i);

    if (line->text == NULL || line->text[0] == '\0')
      continue;

    if (text->len > 0)
      g_string_append_c(text, '\n');
    g_string_append(text, line->text);
  }

  return g_string_free(text, FALSE);
}

static void
swash_window_clear_ocr_widgets(SwashWindow *self)
{
  if (self->ocr_lines != NULL) {
    guint i;

    for (i = 0; i < self->ocr_lines->len; i++) {
      SwashOcrLine *line = g_ptr_array_index(self->ocr_lines, i);

      line->button = NULL;
    }
  }

  while (gtk_widget_get_first_child(GTK_WIDGET(self->ocr_overlay)) != NULL)
    gtk_fixed_remove(self->ocr_overlay, gtk_widget_get_first_child(GTK_WIDGET(self->ocr_overlay)));
}

static void
swash_window_select_ocr_line(SwashWindow  *self,
                                SwashOcrLine *line)
{
  guint i;

  self->selected_ocr_line = line;

  if (self->ocr_lines != NULL) {
    for (i = 0; i < self->ocr_lines->len; i++) {
      SwashOcrLine *candidate = g_ptr_array_index(self->ocr_lines, i);

      if (candidate->button == NULL)
        continue;

      if (candidate == line)
        gtk_widget_add_css_class(candidate->button, "selected");
      else
        gtk_widget_remove_css_class(candidate->button, "selected");
    }
  }

  if (line != NULL) {
    gtk_stack_set_visible_child_name(self->ocr_panel_stack, "selected");
    swash_window_set_ocr_panel_visible(self, TRUE);
  }

  swash_window_update_ocr_panel(self);
}

static void
swash_window_set_ocr_panel_visible(SwashWindow *self,
                                      gboolean        visible)
{
  if (!gtk_widget_get_visible(self->ocr_panel_toggle_container) && visible)
    return;

  if (gtk_toggle_button_get_active(self->ocr_panel_toggle_button) != visible)
    gtk_toggle_button_set_active(self->ocr_panel_toggle_button, visible);

  if (adw_bottom_sheet_get_open(self->ocr_panel_bottom_sheet) != visible)
    adw_bottom_sheet_set_open(self->ocr_panel_bottom_sheet, visible);
}

static void
swash_window_clear_ocr_results(SwashWindow *self)
{
  self->ocr_running = FALSE;
  self->ocr_generation++;
  self->selected_ocr_line = NULL;
  swash_window_clear_ocr_widgets(self);
  g_clear_pointer(&self->ocr_lines, g_ptr_array_unref);
  g_clear_pointer(&self->ocr_all_text, g_free);
  gtk_widget_set_visible(GTK_WIDGET(self->ocr_overlay), FALSE);
  gtk_widget_set_visible(self->ocr_panel_toggle_container, FALSE);
  gtk_toggle_button_set_active(self->ocr_panel_toggle_button, FALSE);
  swash_window_update_ocr_panel(self);
}

static gboolean
swash_window_ocr_is_visible(SwashWindow *self)
{
  return self->active_tool == SWASH_TOOL_OCR
      && self->texture != NULL
      && self->ocr_lines != NULL
      && self->ocr_lines->len > 0;
}

static void
swash_window_ocr_box_clicked(GtkButton *button,
                                gpointer   user_data)
{
  SwashWindow *self = SWASH_WINDOW(user_data);
  guint i;

  if (self->ocr_lines == NULL)
    return;

  for (i = 0; i < self->ocr_lines->len; i++) {
    SwashOcrLine *line = g_ptr_array_index(self->ocr_lines, i);

    if (line->button == GTK_WIDGET(button)) {
      swash_window_select_ocr_line(self, line);
      return;
    }
  }
}

static void
swash_window_add_ocr_button(SwashWindow  *self,
                               SwashOcrLine *line)
{
  GtkWidget *button = gtk_button_new();

  gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
  gtk_widget_set_focusable(button, FALSE);
  gtk_widget_add_css_class(button, "ocr-overlay-box");
  gtk_widget_set_tooltip_text(button, line->text);
  g_signal_connect(button, "clicked", G_CALLBACK(swash_window_ocr_box_clicked), self);

  gtk_fixed_put(self->ocr_overlay, button, 0.0, 0.0);
  line->button = button;
}

void
swash_window_update_ocr_overlay(SwashWindow *self)
{
  const int image_width = self->texture != NULL
                        ? gdk_paintable_get_intrinsic_width(GDK_PAINTABLE(self->texture))
                        : 0;
  const int image_height = self->texture != NULL
                         ? gdk_paintable_get_intrinsic_height(GDK_PAINTABLE(self->texture))
                         : 0;
  const double widget_width = gtk_widget_get_width(GTK_WIDGET(self->drawing_area));
  const double widget_height = gtk_widget_get_height(GTK_WIDGET(self->drawing_area));
  double display_x;
  double display_y;
  double display_width;
  double display_height;
  guint i;

  if (!swash_window_ocr_is_visible(self)) {
    gtk_widget_set_visible(GTK_WIDGET(self->ocr_overlay), FALSE);
    return;
  }

  if (!swash_window_get_display_rect(self,
                                        widget_width,
                                        widget_height,
                                        &display_x,
                                        &display_y,
                                        &display_width,
                                        &display_height)
      || image_width <= 0
      || image_height <= 0) {
    gtk_widget_set_visible(GTK_WIDGET(self->ocr_overlay), FALSE);
    return;
  }

  gtk_widget_set_visible(GTK_WIDGET(self->ocr_overlay), TRUE);

  for (i = 0; i < self->ocr_lines->len; i++) {
    SwashOcrLine *line = g_ptr_array_index(self->ocr_lines, i);
    const double scale_x = display_width / image_width;
    const double scale_y = display_height / image_height;
    const int x = (int) floor(display_x + line->left * scale_x);
    const int y = (int) floor(display_y + line->top * scale_y);
    const int width = MAX(18, (int) ceil(line->width * scale_x));
    const int height = MAX(18, (int) ceil(line->height * scale_y));

    if (line->button == NULL)
      swash_window_add_ocr_button(self, line);

    gtk_widget_set_size_request(line->button, width, height);
    gtk_fixed_move(self->ocr_overlay, line->button, x, y);
  }

  swash_window_select_ocr_line(self, self->selected_ocr_line);
}

void
swash_window_update_ocr_panel(SwashWindow *self)
{
  const gboolean has_results = self->ocr_lines != NULL && self->ocr_lines->len > 0;
  const gboolean can_show_toggle = self->active_tool == SWASH_TOOL_OCR
                                 && self->texture != NULL
                                 && (self->ocr_running || has_results);
  const gboolean show_panel = can_show_toggle
                           && gtk_toggle_button_get_active(self->ocr_panel_toggle_button);
  const char *selected_text = NULL;
  const char *all_text = NULL;

  gtk_widget_set_visible(self->ocr_panel_toggle_container, can_show_toggle);

  if (self->ocr_running && gtk_toggle_button_get_active(self->ocr_panel_toggle_button)) {
    selected_text = "Recognizing text...";
    all_text = "Recognizing text...";
  } else {
    selected_text = self->selected_ocr_line != NULL
                  ? self->selected_ocr_line->text
                  : "Click a highlighted region to inspect its recognized text.";
    all_text = has_results && self->ocr_all_text != NULL && self->ocr_all_text[0] != '\0'
             ? self->ocr_all_text
             : "No OCR text available.";
  }

  swash_window_set_text_view_text(self->ocr_selected_text_view, selected_text);
  swash_window_set_text_view_text(self->ocr_all_text_view, all_text);
  if (adw_bottom_sheet_get_open(self->ocr_panel_bottom_sheet) != show_panel)
    adw_bottom_sheet_set_open(self->ocr_panel_bottom_sheet, show_panel);
}

static void
swash_window_ocr_panel_toggled(GtkToggleButton *button,
                                  gpointer         user_data)
{
  SwashWindow *self = SWASH_WINDOW(user_data);

  if (gtk_toggle_button_get_active(button) && self->selected_ocr_line == NULL)
    gtk_stack_set_visible_child_name(self->ocr_panel_stack, "all");

  swash_window_update_ocr_panel(self);
}

static void
swash_window_ocr_panel_open_changed(GObject    *object,
                                       GParamSpec *pspec,
                                       gpointer    user_data)
{
  SwashWindow *self = SWASH_WINDOW(user_data);
  const gboolean open = adw_bottom_sheet_get_open(self->ocr_panel_bottom_sheet);

  (void) object;
  (void) pspec;

  if (gtk_toggle_button_get_active(self->ocr_panel_toggle_button) != open)
    gtk_toggle_button_set_active(self->ocr_panel_toggle_button, open);

  if (open && self->selected_ocr_line == NULL)
    gtk_stack_set_visible_child_name(self->ocr_panel_stack, "all");
}

static void
swash_window_ocr_panel_close_clicked(GtkButton *button,
                                        gpointer   user_data)
{
  SwashWindow *self = SWASH_WINDOW(user_data);

  (void) button;

  swash_window_set_ocr_panel_visible(self, FALSE);
}

static void
swash_window_ocr_ready(GObject      *source_object,
                          GAsyncResult *result,
                          gpointer      user_data)
{
  SwashWindow *self = SWASH_WINDOW(user_data);
  g_autoptr(GError) error = NULL;
  SwashOcrResult *ocr_result;

  (void) source_object;

  ocr_result = g_task_propagate_pointer(G_TASK(result), &error);
  self->ocr_running = FALSE;

  if (ocr_result == NULL) {
    if (self->active_tool == SWASH_TOOL_OCR)
      swash_window_show_error(self, error->message);
    g_object_unref(self);
    return;
  }

  if (ocr_result->generation != self->ocr_generation) {
    swash_ocr_result_free(ocr_result);
    g_object_unref(self);
    return;
  }

  swash_window_clear_ocr_widgets(self);
  g_clear_pointer(&self->ocr_lines, g_ptr_array_unref);
  g_clear_pointer(&self->ocr_all_text, g_free);
  self->ocr_lines = ocr_result->lines;
  self->ocr_all_text = swash_window_build_ocr_all_text(self->ocr_lines);
  self->selected_ocr_line = NULL;
  ocr_result->lines = NULL;
  swash_window_update_ocr_overlay(self);
  swash_window_update_ocr_panel(self);
  swash_ocr_result_free(ocr_result);
  g_object_unref(self);
}

void
swash_window_maybe_start_ocr(SwashWindow *self)
{
  g_autoptr(GTask) task = NULL;
  SwashOcrRequest *request;

  if (self->texture == NULL || self->ocr_running || self->ocr_lines != NULL)
    return;

  request = swash_ocr_request_new(self->texture, self->ocr_generation);
  if (request == NULL)
    return;

  self->ocr_running = TRUE;
  swash_window_update_ocr_panel(self);
  task = g_task_new(self, NULL, swash_window_ocr_ready, g_object_ref(self));
  g_task_set_task_data(task, request, (GDestroyNotify) swash_ocr_request_free);
  g_task_run_in_thread(task, swash_ocr_run_task);
}

static void swash_window_save_copy_ready(GObject *source_object, GAsyncResult *result, gpointer user_data);
static void swash_window_save_overwrite_action(GtkWidget *widget, const char *action_name, GVariant *parameter);
static void swash_window_save_copy_action(GtkWidget *widget, const char *action_name, GVariant *parameter);
static void swash_window_rotate_counter_clockwise_action(GtkWidget *widget, const char *action_name, GVariant *parameter);
static void swash_window_flip_horizontal_action(GtkWidget *widget, const char *action_name, GVariant *parameter);
static void swash_window_flip_vertical_action(GtkWidget *widget, const char *action_name, GVariant *parameter);
static void swash_window_copy_action(GtkWidget *widget, const char *action_name, GVariant *parameter);
static void swash_window_dismiss_action(GtkWidget *widget, const char *action_name, GVariant *parameter);
static void swash_window_close_window_action(GtkWidget *widget, const char *action_name, GVariant *parameter);
static void swash_window_copy_clicked(GtkButton *button, gpointer user_data);
static void swash_window_open_current_file_action(GtkWidget *widget, const char *action_name, GVariant *parameter);
static void swash_window_open_containing_folder_action(GtkWidget *widget, const char *action_name, GVariant *parameter);
static void swash_window_open_parent_folder(SwashWindow *self);

static gboolean
swash_window_restore_copy_button(gpointer user_data)
{
  SwashWindow *self = SWASH_WINDOW(user_data);

  self->copy_feedback_timeout_id = 0;
  gtk_stack_set_visible_child(self->copy_icon_stack, GTK_WIDGET(self->copy_default_icon));
  g_object_unref(self);
  return G_SOURCE_REMOVE;
}

void
swash_window_trigger_copy(SwashWindow *self)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = NULL;
  SwashExportRequest *request;

  if (self->texture == NULL || self->copy_in_progress)
    return;

  request = swash_export_request_new(self->texture,
                                        swash_window_strokes(self),
                                        SWASH_EXPORT_COPY,
                                        NULL,
                                        "png",
                                        swash_stroke_copy,
                                        (GDestroyNotify) swash_stroke_free,
                                        self->allow_highlighter_overlap,
                                        swash_stroke_render,
                                        swash_document_get_image_generation(self->document),
                                        &error);
  if (request == NULL) {
    swash_window_show_error(self, error->message);
    return;
  }

  self->copy_in_progress = TRUE;
  task = g_task_new(self, NULL, swash_window_copy_export_ready, g_object_ref(self));
  g_task_set_task_data(task, request, (GDestroyNotify) swash_export_request_free);
  g_task_run_in_thread(task, swash_export_run_task);
}

void
swash_window_maybe_auto_copy_latest_change(SwashWindow *self)
{
  if (!self->auto_copy_latest_change || self->texture == NULL || !swash_window_has_unsaved_changes(self))
    return;

  if (self->copy_in_progress) {
    self->auto_copy_pending = TRUE;
    return;
  }

  swash_window_trigger_copy(self);
}

static void
swash_window_flash_copy_success(SwashWindow *self)
{
  if (self->copy_feedback_timeout_id != 0)
    g_source_remove(self->copy_feedback_timeout_id);

  gtk_stack_set_visible_child(self->copy_icon_stack, GTK_WIDGET(self->copy_success_icon));
  self->copy_feedback_timeout_id = g_timeout_add(1200,
                                                 swash_window_restore_copy_button,
                                                 g_object_ref(self));
}

void
swash_window_reset_save_button(SwashWindow *self)
{
  const gboolean has_unsaved_changes = swash_window_has_unsaved_changes(self);

  gtk_stack_set_visible_child(self->save_icon_stack, GTK_WIDGET(self->save_default_icon));
  gtk_widget_set_sensitive(GTK_WIDGET(self->save_button), has_unsaved_changes || self->texture != NULL);
  gtk_widget_action_set_enabled(GTK_WIDGET(self), "win.save", has_unsaved_changes && self->current_file != NULL);
  gtk_widget_action_set_enabled(GTK_WIDGET(self), "win.save-copy", self->texture != NULL);
}

static gboolean
swash_window_restore_save_button(gpointer user_data)
{
  SwashWindow *self = SWASH_WINDOW(user_data);

  self->save_feedback_timeout_id = 0;
  swash_window_reset_save_button(self);
  g_object_unref(self);
  return G_SOURCE_REMOVE;
}

static gboolean
swash_window_show_save_success(gpointer user_data)
{
  SwashWindow *self = SWASH_WINDOW(user_data);

  self->save_spinner_timeout_id = 0;
  gtk_widget_set_sensitive(GTK_WIDGET(self->save_button), FALSE);
  gtk_stack_set_visible_child(self->save_icon_stack, GTK_WIDGET(self->save_success_icon));
  self->save_feedback_timeout_id = g_timeout_add(1200,
                                                 swash_window_restore_save_button,
                                                 g_object_ref(self));
  g_object_unref(self);
  return G_SOURCE_REMOVE;
}

static void
swash_window_begin_save_feedback(SwashWindow *self)
{
  if (self->save_spinner_timeout_id != 0)
    g_source_remove(self->save_spinner_timeout_id);

  if (self->save_feedback_timeout_id != 0)
    g_source_remove(self->save_feedback_timeout_id);

  self->save_spinner_timeout_id = 0;
  self->save_feedback_timeout_id = 0;
  self->save_feedback_started_at = g_get_monotonic_time();

  gtk_stack_set_visible_child(self->save_icon_stack, GTK_WIDGET(self->save_working_icon));
  gtk_widget_action_set_enabled(GTK_WIDGET(self), "win.save", FALSE);
  gtk_widget_action_set_enabled(GTK_WIDGET(self), "win.save-copy", FALSE);
}

static void
swash_window_finish_save_feedback(SwashWindow *self)
{
  const gint64 min_spinner_us = 500 * G_TIME_SPAN_MILLISECOND;
  gint64 elapsed = g_get_monotonic_time() - self->save_feedback_started_at;
  guint delay_ms = 0;

  if (self->save_spinner_timeout_id != 0)
    g_source_remove(self->save_spinner_timeout_id);

  if (elapsed < min_spinner_us)
    delay_ms = (guint) ((min_spinner_us - elapsed + 999) / 1000);

  self->save_spinner_timeout_id = g_timeout_add(delay_ms,
                                                swash_window_show_save_success,
                                                g_object_ref(self));
}


static void
swash_window_show_error(SwashWindow *self,
                           const char     *message)
{
  g_autoptr(GtkAlertDialog) dialog = gtk_alert_dialog_new("%s", message);

  gtk_alert_dialog_show(dialog, GTK_WINDOW(self));
}

static char *
swash_window_make_copy_name(const char *filename)
{
  const char *dot;

  if (filename == NULL || *filename == '\0')
    return g_strdup("image_copy.png");

  dot = strrchr(filename, '.');
  if (dot == NULL || dot == filename)
    return g_strdup_printf("%s_copy", filename);

  return g_strdup_printf("%.*s_copy%s",
                         (int) (dot - filename),
                         filename,
                         dot);
}

static gboolean
swash_window_error_is_user_dismissed(const GError *error)
{
  if (error == NULL)
    return FALSE;

  if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    return TRUE;

  return error->message != NULL
      && g_ascii_strcasecmp(error->message, "Dismissed by user") == 0;
}

static gboolean
swash_window_has_unsaved_changes(SwashWindow *self)
{
  return self->texture != NULL && swash_document_has_unsaved_changes(self->document);
}

static void
swash_window_mark_saved(SwashWindow *self)
{
  swash_document_mark_saved(self->document);
}

void
swash_window_update_history_buttons(SwashWindow *self)
{
  const gboolean has_image = self->texture != NULL;

  gtk_widget_set_sensitive(GTK_WIDGET(self->undo_button), has_image && swash_document_can_undo(self->document));
  gtk_widget_set_sensitive(GTK_WIDGET(self->redo_button), has_image && swash_document_can_redo(self->document));
}

void
swash_window_clear_history(SwashWindow *self)
{
  swash_document_clear_history(self->document);
  swash_window_update_history_buttons(self);
}

void
swash_window_record_undo_step(SwashWindow *self)
{
  swash_document_record_undo_step(self->document);
  swash_window_reset_save_button(self);
  swash_window_update_history_buttons(self);
}

static GBytes *
swash_window_texture_download_bytes(GdkTexture *texture,
                                       int        *width,
                                       int        *height,
                                       gsize      *stride)
{
  guchar *pixels;
  gsize rowstride;
  int image_width;
  int image_height;

  if (texture == NULL)
    return NULL;

  image_width = gdk_texture_get_width(texture);
  image_height = gdk_texture_get_height(texture);
  if (image_width <= 0 || image_height <= 0)
    return NULL;

  rowstride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, image_width);
  pixels = g_malloc(rowstride * image_height);
  gdk_texture_download(texture, pixels, rowstride);

  if (width != NULL)
    *width = image_width;
  if (height != NULL)
    *height = image_height;
  if (stride != NULL)
    *stride = rowstride;

  return g_bytes_new_take(pixels, rowstride * image_height);
}

static GBytes *
swash_window_surface_copy_bytes(cairo_surface_t *surface,
                                   int             *width,
                                   int             *height,
                                   gsize           *stride)
{
  const gsize rowstride = cairo_image_surface_get_stride(surface);
  const int image_width = cairo_image_surface_get_width(surface);
  const int image_height = cairo_image_surface_get_height(surface);
  guchar *pixels;

  if (surface == NULL || cairo_surface_get_type(surface) != CAIRO_SURFACE_TYPE_IMAGE)
    return NULL;

  cairo_surface_flush(surface);
  pixels = g_memdup2(cairo_image_surface_get_data(surface), rowstride * image_height);

  if (width != NULL)
    *width = image_width;
  if (height != NULL)
    *height = image_height;
  if (stride != NULL)
    *stride = rowstride;

  return g_bytes_new_take(pixels, rowstride * image_height);
}

static void
swash_window_copy_image_bytes_to_surface(cairo_surface_t *surface,
                                            GBytes          *pixels,
                                            gsize            stride)
{
  const guchar *src = g_bytes_get_data(pixels, NULL);
  guchar *dst;
  const int height = cairo_image_surface_get_height(surface);
  const gsize dst_stride = cairo_image_surface_get_stride(surface);

  if (src == NULL)
    return;

  dst = cairo_image_surface_get_data(surface);

  for (int y = 0; y < height; y++)
    memcpy(dst + y * dst_stride, src + y * stride, MIN(dst_stride, stride));

  cairo_surface_mark_dirty(surface);
}

static gboolean
swash_window_refresh_image_from_document(SwashWindow *self)
{
  g_autoptr(GBytes) pixels = NULL;
  int width = 0;
  int height = 0;
  gsize stride = 0;

  g_clear_object(&self->texture);
  if (self->image_surface != NULL) {
    cairo_surface_destroy(self->image_surface);
    self->image_surface = NULL;
  }

  if (!swash_document_get_image(self->document, &pixels, &width, &height, &stride)) {
    gtk_picture_set_paintable(self->picture, NULL);
    return FALSE;
  }

  self->texture = GDK_TEXTURE(gdk_memory_texture_new(width,
                                                     height,
                                                     GDK_MEMORY_DEFAULT,
                                                     pixels,
                                                     stride));
  self->image_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  swash_window_copy_image_bytes_to_surface(self->image_surface, pixels, stride);
  gtk_picture_set_paintable(self->picture, GDK_PAINTABLE(self->texture));
  return TRUE;
}

cairo_surface_t *
swash_window_render_composited_surface(SwashWindow *self)
{
  cairo_surface_t *surface;
  cairo_t *cr;
  GPtrArray *strokes = swash_window_strokes(self);

  if (self->texture == NULL)
    return NULL;

  surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                       gdk_texture_get_width(self->texture),
                                       gdk_texture_get_height(self->texture));
  gdk_texture_download(self->texture,
                       cairo_image_surface_get_data(surface),
                       cairo_image_surface_get_stride(surface));
  cairo_surface_mark_dirty(surface);

  cr = cairo_create(surface);
  swash_render_strokes(cr,
                          strokes,
                          surface,
                          self->allow_highlighter_overlap,
                          swash_stroke_render,
                          swash_document_get_image_generation(self->document));
  cairo_destroy(cr);
  cairo_surface_flush(surface);
  return surface;
}

void
swash_window_refresh_document_state(SwashWindow *self)
{
  swash_window_refresh_image_from_document(self);
  self->current_stroke = NULL;
  self->drawing = FALSE;
  self->interaction_has_undo_step = FALSE;
  self->active_touch_draw_sequence = NULL;
  self->cancelled_touch_draw_sequence = NULL;
  self->crop_start_x = 0.0;
  self->crop_start_y = 0.0;
  self->crop_end_x = 0.0;
  self->crop_end_y = 0.0;
  swash_window_clear_ocr_results(self);
  swash_window_apply_zoom_mode(self);
  swash_window_update_zoom_label(self);
  swash_window_sync_state(self);
  gtk_widget_queue_draw(GTK_WIDGET(self->drawing_area));
}

static void
swash_window_log_formats(const char      *label,
                            GdkContentFormats *formats)
{
  g_autofree char *description = NULL;

  if (formats == NULL || gdk_content_formats_is_empty(formats)) {
    g_debug("%s: (none)", label);
    return;
  }

  description = gdk_content_formats_to_string(formats);
  g_debug("%s: %s", label, description);
}

static void
swash_window_copy_export_ready(GObject      *source_object,
                                  GAsyncResult *result,
                                  gpointer      user_data)
{
  SwashWindow *self = SWASH_WINDOW(user_data);
  g_autoptr(GError) error = NULL;
  SwashCopyResult *copy_result;
  g_autoptr(GdkContentProvider) provider = NULL;
  g_autoptr(GdkContentProvider) bytes_provider = NULL;
  g_autoptr(GdkContentProvider) texture_provider = NULL;
  g_autoptr(GdkContentFormats) provider_formats = NULL;
  GdkClipboard *clipboard;

  (void) source_object;

  copy_result = g_task_propagate_pointer(G_TASK(result), &error);
  self->copy_in_progress = FALSE;
  if (copy_result == NULL) {
    swash_window_show_error(self, error->message);
    if (self->auto_copy_pending) {
      self->auto_copy_pending = FALSE;
      swash_window_maybe_auto_copy_latest_change(self);
    }
    g_object_unref(self);
    return;
  }

  clipboard = gdk_display_get_clipboard(gtk_widget_get_display(GTK_WIDGET(self)));
  bytes_provider = gdk_content_provider_new_for_bytes(copy_result->mime_type, copy_result->bytes);

  if (copy_result->texture != NULL) {
    GdkContentProvider *providers[2];

    texture_provider = gdk_content_provider_new_typed(GDK_TYPE_TEXTURE, copy_result->texture);
    providers[0] = g_steal_pointer(&texture_provider);
    providers[1] = g_steal_pointer(&bytes_provider);
    provider = gdk_content_provider_new_union(providers, G_N_ELEMENTS(providers));
  } else {
    provider = g_object_ref(bytes_provider);
  }

  provider_formats = gdk_content_provider_ref_formats(provider);
  swash_window_log_formats("Clipboard provider formats", provider_formats);

  if (!gdk_clipboard_set_content(clipboard, provider)) {
    swash_copy_result_free(copy_result);
    swash_window_show_error(self, "Could not copy image to clipboard");
    g_object_unref(self);
    return;
  }

  swash_window_log_formats("Clipboard accepted formats", gdk_clipboard_get_formats(clipboard));
  swash_window_flash_copy_success(self);
  swash_copy_result_free(copy_result);
  if (self->auto_copy_pending) {
    self->auto_copy_pending = FALSE;
    swash_window_maybe_auto_copy_latest_change(self);
  }
  g_object_unref(self);
}

static void
swash_window_save_export_ready(GObject      *source_object,
                                  GAsyncResult *result,
                                  gpointer      user_data)
{
  SwashWindow *self = SWASH_WINDOW(user_data);
  g_autoptr(GError) error = NULL;

  (void) source_object;

  if (!g_task_propagate_boolean(G_TASK(result), &error)) {
    if (self->save_spinner_timeout_id != 0)
      g_source_remove(self->save_spinner_timeout_id);

    if (self->save_feedback_timeout_id != 0)
      g_source_remove(self->save_feedback_timeout_id);

    self->save_spinner_timeout_id = 0;
    self->save_feedback_timeout_id = 0;
    swash_window_reset_save_button(self);
    swash_window_show_error(self, error->message);
    g_object_unref(self);
    return;
  }

  swash_window_mark_saved(self);
  swash_window_finish_save_feedback(self);
  g_object_unref(self);
}

static void
swash_window_save_copy_ready(GObject      *source_object,
                                GAsyncResult *result,
                                gpointer      user_data)
{
  SwashWindow *self = SWASH_WINDOW(user_data);
  g_autoptr(GError) error = NULL;
  g_autoptr(GFile) file = NULL;
  g_autoptr(GTask) task = NULL;
  SwashExportRequest *request;

  file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source_object), result, &error);
  if (file == NULL) {
    if (!swash_window_error_is_user_dismissed(error))
      swash_window_show_error(self, error->message);

    g_object_unref(self);
    return;
  }

  request = swash_export_request_new(self->texture,
                                        swash_window_strokes(self),
                                        SWASH_EXPORT_SAVE,
                                        file,
                                        NULL,
                                        swash_stroke_copy,
                                        (GDestroyNotify) swash_stroke_free,
                                        self->allow_highlighter_overlap,
                                        swash_stroke_render,
                                        swash_document_get_image_generation(self->document),
                                        &error);
  if (request == NULL) {
    swash_window_show_error(self, error->message);
    g_object_unref(self);
    return;
  }

  swash_window_begin_save_feedback(self);
  task = g_task_new(self, NULL, swash_window_save_export_ready, g_object_ref(self));
  g_task_set_task_data(task, request, (GDestroyNotify) swash_export_request_free);
  g_task_run_in_thread(task, swash_export_run_task);

  g_object_unref(self);
}

static void
swash_window_save_overwrite_action(GtkWidget  *widget,
                                      const char *action_name,
                                      GVariant   *parameter)
{
  SwashWindow *self = SWASH_WINDOW(widget);
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = NULL;
  SwashExportRequest *request;

  (void) action_name;
  (void) parameter;

  if (self->current_file == NULL)
    return;

  request = swash_export_request_new(self->texture,
                                        swash_window_strokes(self),
                                        SWASH_EXPORT_SAVE,
                                        self->current_file,
                                        NULL,
                                        swash_stroke_copy,
                                        (GDestroyNotify) swash_stroke_free,
                                        self->allow_highlighter_overlap,
                                        swash_stroke_render,
                                        swash_document_get_image_generation(self->document),
                                        &error);
  if (request == NULL) {
    swash_window_show_error(self, error->message);
    return;
  }

  swash_window_begin_save_feedback(self);
  task = g_task_new(self, NULL, swash_window_save_export_ready, g_object_ref(self));
  g_task_set_task_data(task, request, (GDestroyNotify) swash_export_request_free);
  g_task_run_in_thread(task, swash_export_run_task);
}

static void
swash_window_save_copy_action(GtkWidget  *widget,
                                 const char *action_name,
                                 GVariant   *parameter)
{
  SwashWindow *self = SWASH_WINDOW(widget);
  g_autoptr(GtkFileDialog) dialog = gtk_file_dialog_new();
  g_autofree char *basename = NULL;
  g_autofree char *copy_name = NULL;

  (void) action_name;
  (void) parameter;

  if (self->current_file != NULL)
    basename = g_file_get_basename(self->current_file);

  copy_name = swash_window_make_copy_name(basename != NULL ? basename : self->source_name);

  gtk_file_dialog_set_title(dialog, "Save as copy");
  gtk_file_dialog_set_initial_name(dialog, copy_name);

  gtk_file_dialog_save(dialog,
                       GTK_WINDOW(self),
                       NULL,
                       swash_window_save_copy_ready,
                       g_object_ref(self));
}

static void
swash_window_commit_transformed_surface(SwashWindow  *self,
                                           cairo_surface_t *surface)
{
  g_autoptr(GBytes) pixels = NULL;
  int width = 0;
  int height = 0;
  gsize stride = 0;

  pixels = swash_window_surface_copy_bytes(surface, &width, &height, &stride);
  if (pixels == NULL)
    return;

  swash_document_set_image(self->document, pixels, width, height, stride);
  swash_document_clear_annotations(self->document);
  swash_window_restore_strokes(self, swash_window_strokes(self));
}

void
swash_window_apply_crop(SwashWindow *self,
                           int             left,
                           int             top,
                           int             width,
                           int             height)
{
  cairo_surface_t *source_surface;
  cairo_surface_t *result_surface;
  cairo_t *cr;

  if (self->texture == NULL || width <= 0 || height <= 0)
    return;

  source_surface = swash_window_render_composited_surface(self);
  if (source_surface == NULL)
    return;

  result_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  cr = cairo_create(result_surface);
  cairo_set_source_surface(cr, source_surface, -left, -top);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_destroy(source_surface);

  swash_window_record_undo_step(self);
  swash_window_commit_transformed_surface(self, result_surface);
  cairo_surface_destroy(result_surface);
}

static void
swash_window_rotate_counter_clockwise_action(GtkWidget  *widget,
                                                const char *action_name,
                                                GVariant   *parameter)
{
  SwashWindow *self = SWASH_WINDOW(widget);
  cairo_surface_t *source_surface;
  cairo_surface_t *result_surface;
  cairo_t *cr;
  const int width = self->texture != NULL ? gdk_texture_get_width(self->texture) : 0;
  const int height = self->texture != NULL ? gdk_texture_get_height(self->texture) : 0;

  (void) action_name;
  (void) parameter;

  if (width <= 0 || height <= 0)
    return;

  source_surface = swash_window_render_composited_surface(self);
  if (source_surface == NULL)
    return;

  result_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, height, width);
  cr = cairo_create(result_surface);
  cairo_translate(cr, 0.0, width);
  cairo_rotate(cr, -G_PI / 2.0);
  cairo_set_source_surface(cr, source_surface, 0.0, 0.0);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_destroy(source_surface);

  swash_window_record_undo_step(self);
  swash_window_commit_transformed_surface(self, result_surface);
  cairo_surface_destroy(result_surface);
}

static void
swash_window_flip_horizontal_action(GtkWidget  *widget,
                                       const char *action_name,
                                       GVariant   *parameter)
{
  SwashWindow *self = SWASH_WINDOW(widget);
  cairo_surface_t *source_surface;
  cairo_surface_t *result_surface;
  cairo_t *cr;
  const int width = self->texture != NULL ? gdk_texture_get_width(self->texture) : 0;
  const int height = self->texture != NULL ? gdk_texture_get_height(self->texture) : 0;

  (void) action_name;
  (void) parameter;

  if (width <= 0 || height <= 0)
    return;

  source_surface = swash_window_render_composited_surface(self);
  if (source_surface == NULL)
    return;

  result_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  cr = cairo_create(result_surface);
  cairo_translate(cr, width, 0.0);
  cairo_scale(cr, -1.0, 1.0);
  cairo_set_source_surface(cr, source_surface, 0.0, 0.0);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_destroy(source_surface);

  swash_window_record_undo_step(self);
  swash_window_commit_transformed_surface(self, result_surface);
  cairo_surface_destroy(result_surface);
}

static void
swash_window_flip_vertical_action(GtkWidget  *widget,
                                     const char *action_name,
                                     GVariant   *parameter)
{
  SwashWindow *self = SWASH_WINDOW(widget);
  cairo_surface_t *source_surface;
  cairo_surface_t *result_surface;
  cairo_t *cr;
  const int width = self->texture != NULL ? gdk_texture_get_width(self->texture) : 0;
  const int height = self->texture != NULL ? gdk_texture_get_height(self->texture) : 0;

  (void) action_name;
  (void) parameter;

  if (width <= 0 || height <= 0)
    return;

  source_surface = swash_window_render_composited_surface(self);
  if (source_surface == NULL)
    return;

  result_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  cr = cairo_create(result_surface);
  cairo_translate(cr, 0.0, height);
  cairo_scale(cr, 1.0, -1.0);
  cairo_set_source_surface(cr, source_surface, 0.0, 0.0);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_destroy(source_surface);

  swash_window_record_undo_step(self);
  swash_window_commit_transformed_surface(self, result_surface);
  cairo_surface_destroy(result_surface);
}

static void
swash_window_copy_action(GtkWidget  *widget,
                            const char *action_name,
                            GVariant   *parameter)
{
  SwashWindow *self = SWASH_WINDOW(widget);

  (void) action_name;
  (void) parameter;

  if (!gtk_widget_is_sensitive(GTK_WIDGET(self->copy_button)))
    return;

  gtk_widget_activate(GTK_WIDGET(self->copy_button));
}

static void
swash_window_dismiss_action(GtkWidget  *widget,
                               const char *action_name,
                               GVariant   *parameter)
{
  SwashWindow *self = SWASH_WINDOW(widget);

  (void) action_name;
  (void) parameter;

  if (adw_bottom_sheet_get_open(self->ocr_panel_bottom_sheet))
    swash_window_set_ocr_panel_visible(self, FALSE);
}

static void
swash_window_close_window_action(GtkWidget  *widget,
                                    const char *action_name,
                                    GVariant   *parameter)
{
  (void) action_name;
  (void) parameter;

  gtk_window_destroy(GTK_WINDOW(widget));
}

static void
swash_window_copy_clicked(GtkButton *button,
                             gpointer   user_data)
{
  SwashWindow *self = SWASH_WINDOW(user_data);

  (void) button;

  swash_window_trigger_copy(self);
}

static void
swash_window_clear_annotations(SwashWindow *self)
{
  swash_document_clear_annotations(self->document);
  self->current_stroke = NULL;
  gtk_widget_queue_draw(GTK_WIDGET(self->drawing_area));
  swash_window_update_history_buttons(self);
  swash_window_maybe_auto_copy_latest_change(self);
}

static void
swash_window_clear_image(SwashWindow *self)
{
  g_clear_object(&self->current_file);
  g_clear_object(&self->texture);
  if (self->image_surface != NULL) {
    cairo_surface_destroy(self->image_surface);
    self->image_surface = NULL;
  }
  g_clear_pointer(&self->source_name, g_free);
  self->crop_start_x = 0.0;
  self->crop_start_y = 0.0;
  self->crop_end_x = 0.0;
  self->crop_end_y = 0.0;
  swash_document_set_image(self->document, NULL, 0, 0, 0);
  swash_window_clear_ocr_results(self);
  swash_window_clear_history(self);
  swash_window_clear_annotations(self);
  swash_window_mark_saved(self);
  gtk_picture_set_paintable(self->picture, NULL);
  self->fit_mode = TRUE;
  swash_window_update_picture_size(self);
  swash_window_sync_state(self);
}

static void
swash_window_set_image(SwashWindow *self,
                          GdkTexture     *texture,
                          GFile          *file,
                          const char     *display_name)
{
  g_autoptr(GBytes) pixels = NULL;
  int width = 0;
  int height = 0;
  gsize stride = 0;

  swash_window_clear_image(self);

  self->current_file = file != NULL ? g_object_ref(file) : NULL;
  pixels = swash_window_texture_download_bytes(texture, &width, &height, &stride);
  if (pixels == NULL)
    return;

  swash_document_set_image(self->document, pixels, width, height, stride);
  swash_window_refresh_image_from_document(self);

  self->source_name = g_strdup(display_name != NULL ? display_name : "image.png");

  gtk_label_set_text(self->file_label, self->source_name);
  self->zoom = 1.0;
  self->fit_mode = TRUE;
  self->drawing = FALSE;
  self->current_stroke = NULL;
  self->ocr_running = FALSE;
  swash_window_mark_saved(self);

  swash_window_sync_state(self);
  swash_window_queue_fit_zoom(self);
  gtk_widget_queue_draw(GTK_WIDGET(self->drawing_area));
}

gboolean
swash_window_open_file(SwashWindow *self,
                          GFile          *file,
                          GError        **error)
{
  g_autofree char *basename = NULL;
  g_autoptr(GdkTexture) texture = NULL;

  g_return_val_if_fail(SWASH_IS_WINDOW(self), FALSE);
  g_return_val_if_fail(file == NULL || G_IS_FILE(file), FALSE);

  if (file == NULL) {
    swash_window_clear_image(self);
    return TRUE;
  }

  texture = gdk_texture_new_from_file(file, error);
  if (texture == NULL)
    return FALSE;

  basename = g_file_get_basename(file);
  swash_window_set_image(self, texture, file, basename);
  return TRUE;
}

gboolean
swash_window_open_bytes(SwashWindow *self,
                           GBytes         *bytes,
                           const char     *display_name,
                           GError        **error)
{
  g_autoptr(GdkTexture) texture = NULL;

  g_return_val_if_fail(SWASH_IS_WINDOW(self), FALSE);
  g_return_val_if_fail(bytes != NULL, FALSE);

  texture = gdk_texture_new_from_bytes(bytes, error);
  if (texture == NULL)
    return FALSE;

  swash_window_set_image(self,
                            texture,
                            NULL,
                            display_name != NULL ? display_name : "stdin.png");
  return TRUE;
}

static void
swash_window_open_ready(GObject      *source_object,
                           GAsyncResult *result,
                           gpointer      user_data)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GFile) file = NULL;
  SwashWindow *self = SWASH_WINDOW(user_data);

  file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source_object), result, &error);

  if (file == NULL) {
    if (!swash_window_error_is_user_dismissed(error))
      g_printerr("Error opening file: %s\n", error->message);

    g_object_unref(self);
    return;
  }

  if (!swash_window_open_file(self, file, &error))
    swash_window_show_error(self, error->message);

  g_object_unref(self);
}

static void
swash_window_open(SwashWindow *self)
{
  g_autoptr(GtkFileDialog) dialog = NULL;
  g_autoptr(GtkFileFilter) images = NULL;
  g_autoptr(GListStore) filters = NULL;

  dialog = gtk_file_dialog_new();
  images = gtk_file_filter_new();
  filters = g_list_store_new(GTK_TYPE_FILE_FILTER);

  gtk_file_filter_set_name(images, "Images");
  gtk_file_filter_add_mime_type(images, "image/png");
  gtk_file_filter_add_mime_type(images, "image/jpeg");
  gtk_file_filter_add_mime_type(images, "image/webp");
  gtk_file_filter_add_mime_type(images, "image/gif");
  gtk_file_filter_add_mime_type(images, "image/bmp");
  gtk_file_filter_add_mime_type(images, "image/svg+xml");
  g_list_store_append(filters, images);

  gtk_file_dialog_set_title(dialog, "Open image");
  gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
  gtk_file_dialog_set_default_filter(dialog, images);

  gtk_file_dialog_open(dialog,
                       GTK_WINDOW(self),
                       NULL,
                       swash_window_open_ready,
                       g_object_ref(self));
}

static void
swash_window_open_action(GtkWidget  *widget,
                            const char *action_name,
                            GVariant   *parameter)
{
  (void) action_name;
  (void) parameter;

  swash_window_open(SWASH_WINDOW(widget));
}

static void
swash_window_open_current_file_ready(GObject      *source_object,
                                        GAsyncResult *result,
                                        gpointer      user_data)
{
  SwashWindow *self = SWASH_WINDOW(user_data);
  g_autoptr(GError) error = NULL;

  if (!gtk_file_launcher_launch_finish(GTK_FILE_LAUNCHER(source_object), result, &error)
      && !swash_window_error_is_user_dismissed(error))
    swash_window_show_error(self, error->message);

  g_object_unref(self);
}

static void
swash_window_open_current_file_action(GtkWidget  *widget,
                                         const char *action_name,
                                         GVariant   *parameter)
{
  SwashWindow *self = SWASH_WINDOW(widget);
  g_autoptr(GtkFileLauncher) launcher = NULL;

  (void) action_name;
  (void) parameter;

  if (self->current_file == NULL)
    return;

  launcher = gtk_file_launcher_new(self->current_file);
  gtk_file_launcher_launch(launcher,
                           GTK_WINDOW(self),
                           NULL,
                           swash_window_open_current_file_ready,
                           g_object_ref(self));
}

static void
swash_window_open_parent_folder(SwashWindow *self)
{
  g_autoptr(GtkFileLauncher) launcher = NULL;
  g_autoptr(GFile) parent = NULL;

  if (self->current_file == NULL)
    return;

  parent = g_file_get_parent(self->current_file);
  if (parent == NULL)
    return;

  launcher = gtk_file_launcher_new(parent);
  gtk_file_launcher_launch(launcher,
                           GTK_WINDOW(self),
                           NULL,
                           swash_window_open_current_file_ready,
                           g_object_ref(self));
}

static void
swash_window_open_containing_folder_action(GtkWidget  *widget,
                                              const char *action_name,
                                              GVariant   *parameter)
{
  SwashWindow *self = SWASH_WINDOW(widget);
  g_autoptr(GDBusConnection) bus = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *uri = NULL;

  (void) action_name;
  (void) parameter;

  if (self->current_file == NULL)
    return;

  uri = g_file_get_uri(self->current_file);
  if (uri == NULL)
    return;

  bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
  if (bus == NULL) {
    g_clear_error(&error);
    swash_window_open_parent_folder(self);
    return;
  }
  //if a file manager supporting the FileManager1 D-Bus interface is available
  //use it to show the file in its containing folder, otherwise
  //fall back to opening the parent folder without highlighting the file
  if (!g_dbus_connection_call_sync(bus,
                                   "org.freedesktop.FileManager1",
                                   "/org/freedesktop/FileManager1",
                                   "org.freedesktop.FileManager1",
                                   "ShowItems",
                                   g_variant_new("(^ass)", (const char *[]) {uri, NULL}, ""),
                                   NULL,
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1,
                                   NULL,
                                   &error)) {
    g_clear_error(&error);
    swash_window_open_parent_folder(self);
  }
}

static void
swash_window_dispose(GObject *object)
{
  SwashWindow *self = SWASH_WINDOW(object);

  swash_window_save_preferences(self);

  if (self->text_cursor_blink_id != 0) {
    g_source_remove(self->text_cursor_blink_id);
    self->text_cursor_blink_id = 0;
  }

  g_clear_object(&self->current_file);
  g_clear_pointer(&self->source_name, g_free);
  g_clear_object(&self->texture);
  if (self->image_surface != NULL) {
    cairo_surface_destroy(self->image_surface);
    self->image_surface = NULL;
  }
  swash_window_clear_ocr_results(self);
  swash_window_clear_annotations(self);
  if (self->copy_feedback_timeout_id != 0)
    g_source_remove(self->copy_feedback_timeout_id);
  if (self->save_spinner_timeout_id != 0)
    g_source_remove(self->save_spinner_timeout_id);
  if (self->save_feedback_timeout_id != 0)
    g_source_remove(self->save_feedback_timeout_id);
  swash_window_clear_history(self);
  g_clear_pointer(&self->document, swash_document_free);
  g_clear_object(&self->start_window_controls_children);
  g_clear_object(&self->end_window_controls_children);
  if (self->window_css_provider != NULL && gdk_display_get_default() != NULL)
    gtk_style_context_remove_provider_for_display(gdk_display_get_default(),
                                                  GTK_STYLE_PROVIDER(self->window_css_provider));
  g_clear_object(&self->window_css_provider);
  if (self->widget_css_provider != NULL && gdk_display_get_default() != NULL)
    gtk_style_context_remove_provider_for_display(gdk_display_get_default(),
                                                  GTK_STYLE_PROVIDER(self->widget_css_provider));
  g_clear_object(&self->widget_css_provider);
  g_clear_pointer(&self->copy_shortcut_accel, g_free);
  g_clear_pointer(&self->active_touch_sequences, g_hash_table_unref);
  g_clear_pointer(&self->touch_tap_points, g_hash_table_unref);

  G_OBJECT_CLASS(swash_window_parent_class)->dispose(object);
}

static void
swash_window_bind_template_children(GtkWidgetClass *widget_class)
{
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, canvas_stack);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, canvas_scroller);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, empty_page);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, canvas_surface);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, picture);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, drawing_area);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, ocr_overlay);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, ocr_panel_bottom_sheet);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, ocr_panel);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, ocr_panel_toggle_container);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, ocr_panel_toggle_button);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, ocr_panel_close_button);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, ocr_panel_stack);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, ocr_panel_tabs);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, ocr_selected_page);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, ocr_all_page);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, ocr_selected_text_view);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, ocr_all_text_view);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, start_actions);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, start_window_controls_pill);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, start_window_controls);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, open_actions);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, file_group);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, file_button);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, file_label);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, zoom_label);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, tool_group);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, pan_tool_button);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, crop_tool_button);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, brush_tool_button);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, highlighter_tool_button);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, eraser_tool_button);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, rectangle_tool_button);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, circle_tool_button);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, line_tool_button);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, arrow_tool_button);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, ocr_tool_button);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, text_tool_button);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, blur_tool_button);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, numbering_tool_button);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, move_tool_button);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, rotate_counter_clockwise_button);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, flip_horizontal_button);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, flip_vertical_button);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, history_actions);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, undo_button);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, redo_button);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, document_actions);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, save_button);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, save_icon_stack);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, save_default_icon);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, save_working_icon);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, save_success_icon);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, copy_button);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, app_menu_button);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, end_window_controls);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, copy_icon_stack);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, copy_default_icon);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, copy_success_icon);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, zoom_group);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, fit_zoom_button);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, settings_group);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, color_button);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, fill_color_button);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, width_scale);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, size_button);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, size_button_label);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, text_size_spin);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, precise_size_spin);
  gtk_widget_class_bind_template_child(widget_class, SwashWindow, blur_type_dropdown);
}

static void
swash_window_install_actions(GtkWidgetClass *widget_class)
{
  gtk_widget_class_install_action(widget_class, "win.open", NULL, swash_window_open_action);
  gtk_widget_class_install_action(widget_class, "win.open-current-file", NULL, swash_window_open_current_file_action);
  gtk_widget_class_install_action(widget_class, "win.open-containing-folder", NULL, swash_window_open_containing_folder_action);
  gtk_widget_class_install_action(widget_class, "win.copy-buffer", NULL, swash_window_copy_action);
  gtk_widget_class_install_action(widget_class, "win.dismiss", NULL, swash_window_dismiss_action);
  gtk_widget_class_install_action(widget_class, "win.close-window", NULL, swash_window_close_window_action);
  gtk_widget_class_install_action(widget_class, "win.save", NULL, swash_window_save_overwrite_action);
  gtk_widget_class_install_action(widget_class, "win.save-copy", NULL, swash_window_save_copy_action);
  gtk_widget_class_install_action(widget_class, "win.rotate-counter-clockwise", NULL, swash_window_rotate_counter_clockwise_action);
  gtk_widget_class_install_action(widget_class, "win.flip-horizontal", NULL, swash_window_flip_horizontal_action);
  gtk_widget_class_install_action(widget_class, "win.flip-vertical", NULL, swash_window_flip_vertical_action);
  gtk_widget_class_install_action(widget_class, "win.preferences", NULL, swash_window_preferences_action);
  gtk_widget_class_install_action(widget_class, "win.about", NULL, swash_window_about_action);
  swash_window_install_canvas_actions(widget_class);
}

static void
swash_window_init_state(SwashWindow *self)
{
  self->zoom = 1.0;
  self->fit_mode = TRUE;
  self->active_tool = SWASH_TOOL_BRUSH;
  self->drawing = FALSE;
  self->document = swash_document_new();
  self->active_touch_sequences = g_hash_table_new(g_direct_hash, g_direct_equal);
  self->touch_tap_points = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
  self->ocr_lines = NULL;
  self->selected_ocr_line = NULL;
  self->ocr_all_text = NULL;
  self->pinch_start_zoom = 1.0;
  self->pointer_in = FALSE;
  self->eraser_style = SWASH_ERASER_STYLE_DUAL_RING;
  self->window_background_mode = SWASH_WINDOW_BACKGROUND_OPAQUE;
  self->window_background_opacity = 0.8;
  self->esc_closes_window = TRUE;
  self->copy_shortcut_enabled = TRUE;
  self->angle_snap_modifiers = GDK_SHIFT_MASK;
  self->allow_highlighter_overlap = TRUE;
  self->floating_controls_blur = TRUE;
  self->auto_copy_latest_change = FALSE;
  self->floating_controls_opacity = 0.7;
  swash_window_apply_copy_shortcut(self, "<Primary>c");
  swash_window_load_preferences(self);

  for (int i = 0; i <= SWASH_TOOL_MOVE; i++)
    self->tool_widths[i] = swash_tool_width(i);

  swash_window_apply_default_tool_colors(self);
  swash_window_load_state(self);
}

static void
swash_window_setup_window_background(SwashWindow *self)
{
  self->window_css_provider = gtk_css_provider_new();
  self->widget_css_provider = gtk_css_provider_new();
  gtk_widget_add_css_class(GTK_WIDGET(self), "window");
  gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                             GTK_STYLE_PROVIDER(self->window_css_provider),
                                             SWASH_WINDOW_STYLE_PROVIDER_PRIORITY); 
  gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                             GTK_STYLE_PROVIDER(self->widget_css_provider),
                                             SWASH_WINDOW_STYLE_PROVIDER_PRIORITY);
  swash_window_update_window_background(self);
  swash_window_update_widget_appearance(self);
}

static void
swash_window_ensure_icons_registered(void)
{
  static gboolean icons_registered = FALSE;

  if (!icons_registered) {
    GtkIconTheme *icon_theme = gtk_icon_theme_get_for_display(gdk_display_get_default());

    gtk_icon_theme_add_resource_path(icon_theme, SWASH_RESOURCE_PREFIX "/icons/hicolor");
    icons_registered = TRUE;
  }
}

static void
swash_window_ensure_css_loaded(void)
{
  static gboolean css_loaded = FALSE;

  if (!css_loaded) {
    GtkCssProvider *provider = gtk_css_provider_new();

    gtk_css_provider_load_from_resource(provider, SWASH_RESOURCE_PREFIX "/ui/style.css");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                               GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    css_loaded = TRUE;
  }
}

static void
swash_window_setup_ocr_panel(SwashWindow *self)
{
  gtk_text_view_set_monospace(self->ocr_selected_text_view, FALSE);
  gtk_text_view_set_monospace(self->ocr_all_text_view, FALSE);
  gtk_stack_page_set_name(gtk_stack_get_page(self->ocr_panel_stack, GTK_WIDGET(self->ocr_selected_page)), "selected");
  gtk_stack_page_set_title(gtk_stack_get_page(self->ocr_panel_stack, GTK_WIDGET(self->ocr_selected_page)), "Selected region");
  gtk_stack_page_set_name(gtk_stack_get_page(self->ocr_panel_stack, GTK_WIDGET(self->ocr_all_page)), "all");
  gtk_stack_page_set_title(gtk_stack_get_page(self->ocr_panel_stack, GTK_WIDGET(self->ocr_all_page)), "All detected text");
  gtk_stack_set_visible_child_name(self->ocr_panel_stack, "all");
}

static void
swash_window_setup_window_controls(SwashWindow *self)
{
  self->start_window_controls_children = gtk_widget_observe_children(GTK_WIDGET(self->start_window_controls));
  self->end_window_controls_children = gtk_widget_observe_children(GTK_WIDGET(self->end_window_controls));

  g_signal_connect(self->start_window_controls_children,
                   "items-changed",
                   G_CALLBACK(swash_window_window_controls_changed),
                   self);
  g_signal_connect(self->end_window_controls_children,
                   "items-changed",
                   G_CALLBACK(swash_window_window_controls_changed),
                   self);

  swash_window_update_window_controls(self);
}

static void
swash_window_setup_menu_button_popover(GtkMenuButton   *button,
                                          GtkArrowType     direction,
                                          GtkPositionType  position)
{
  GtkPopover *popover;

  gtk_menu_button_set_direction(button, direction);

  popover = gtk_menu_button_get_popover(button);
  if (popover == NULL) {
    GMenuModel *model = gtk_menu_button_get_menu_model(button);

    if (model != NULL) {
      GtkWidget *popover_widget = gtk_popover_menu_new_from_model(model);

      gtk_menu_button_set_popover(button, popover_widget);
      popover = GTK_POPOVER(popover_widget);
    }
  }

  if (popover == NULL)
    return;

  gtk_popover_set_has_arrow(popover, FALSE);
  gtk_popover_set_position(popover, position);
}

static void
swash_window_setup_popovers(SwashWindow *self)
{
  swash_window_setup_menu_button_popover(self->save_button,
                                            GTK_ARROW_DOWN,
                                            GTK_POS_BOTTOM);
  swash_window_setup_menu_button_popover(self->app_menu_button,
                                            GTK_ARROW_DOWN,
                                            GTK_POS_BOTTOM);
  swash_window_setup_menu_button_popover(self->file_button,
                                            GTK_ARROW_UP,
                                            GTK_POS_TOP);
  swash_window_setup_menu_button_popover(self->size_button,
                                            GTK_ARROW_UP,
                                            GTK_POS_TOP);
}

static void
swash_window_class_init(SwashWindowClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = swash_window_dispose;

  gtk_widget_class_set_template_from_resource(widget_class, SWASH_RESOURCE_PREFIX "/ui/window.ui");

  swash_window_bind_template_children(widget_class);
  swash_window_install_actions(widget_class);
}

static GtkToggleButton *
swash_window_button_for_tool(SwashWindow *self,
                                SwashTool    tool)
{
  switch (tool) {
  case SWASH_TOOL_PAN:       return self->pan_tool_button;
  case SWASH_TOOL_CROP:      return self->crop_tool_button;
  case SWASH_TOOL_BRUSH:     return self->brush_tool_button;
  case SWASH_TOOL_MARKER:    return self->highlighter_tool_button;
  case SWASH_TOOL_ERASER:    return self->eraser_tool_button;
  case SWASH_TOOL_RECTANGLE: return self->rectangle_tool_button;
  case SWASH_TOOL_CIRCLE:    return self->circle_tool_button;
  case SWASH_TOOL_LINE:      return self->line_tool_button;
  case SWASH_TOOL_ARROW:     return self->arrow_tool_button;
  case SWASH_TOOL_OCR:       return self->ocr_tool_button;
  case SWASH_TOOL_TEXT:      return self->text_tool_button;
  case SWASH_TOOL_BLUR:      return self->blur_tool_button;
  case SWASH_TOOL_NUMBERING: return self->numbering_tool_button;
  case SWASH_TOOL_MOVE:      return self->move_tool_button;
  default:                      return self->brush_tool_button;
  }
}

static void
swash_window_activate_tool_button(SwashWindow *self)
{
  GtkToggleButton *button = swash_window_button_for_tool(self, self->active_tool);

  if (button != NULL && !gtk_toggle_button_get_active(button))
    gtk_toggle_button_set_active(button, TRUE);

  self->updating_ui = TRUE;
  gtk_range_set_value(GTK_RANGE(self->width_scale), self->tool_widths[self->active_tool]);
  gtk_color_dialog_button_set_rgba(self->color_button, &self->tool_colors[self->active_tool]);
  gtk_color_dialog_button_set_rgba(self->fill_color_button, &self->tool_fill_colors[self->active_tool]);
  if (self->active_tool == SWASH_TOOL_BLUR)
    gtk_drop_down_set_selected(self->blur_type_dropdown, self->blur_type);
  self->updating_ui = FALSE;
}

static void
swash_window_init(SwashWindow *self)
{
  swash_window_init_state(self);
  swash_window_ensure_icons_registered();

  gtk_widget_init_template(GTK_WIDGET(self));
  swash_window_setup_window_background(self);
  swash_window_ensure_css_loaded();

  gtk_drawing_area_set_draw_func(self->drawing_area,
                                 swash_window_drawing_area_draw,
                                 self,
                                 NULL);
  swash_window_setup_ocr_panel(self);
  swash_window_setup_window_controls(self);
  swash_window_setup_popovers(self);
  swash_window_setup_controllers(self);
  swash_window_setup_signals(self);
  g_signal_connect(self->copy_button, "clicked", G_CALLBACK(swash_window_copy_clicked), self);
  g_signal_connect(self->ocr_panel_toggle_button, "toggled", G_CALLBACK(swash_window_ocr_panel_toggled), self);
  g_signal_connect(self->ocr_panel_close_button, "clicked", G_CALLBACK(swash_window_ocr_panel_close_clicked), self);
  g_signal_connect(self->ocr_panel_bottom_sheet, "notify::open", G_CALLBACK(swash_window_ocr_panel_open_changed), self);

  swash_window_activate_tool_button(self);
  swash_window_update_size_controls(self);
  swash_window_update_tool_ui(self);
  swash_window_sync_state(self);
}

void
swash_window_save_state(SwashWindow *self)
{
  swash_window_save_preferences(self);
}

SwashWindow *
swash_window_new(AdwApplication *app)
{
  return g_object_new(SWASH_TYPE_WINDOW,
                      "application", app,
                      NULL);
}
