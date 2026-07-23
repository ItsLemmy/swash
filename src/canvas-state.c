#include "window-private.h"

#include "editor-utils.h"
#include "stroke.h"

#include <math.h>

static gboolean
swash_tool_has_size_control(SwashTool tool)
{
  return tool != SWASH_TOOL_PAN
      && tool != SWASH_TOOL_CROP
      && tool != SWASH_TOOL_OCR
      && tool != SWASH_TOOL_MOVE;
}

void
swash_window_update_size_controls(SwashWindow *self)
{
  const double value = self->tool_widths[self->active_tool];
  const double small = swash_tool_size_preset(self->active_tool, 0.5);
  const double medium = swash_tool_size_preset(self->active_tool, 1.0);
  const double large = swash_tool_size_preset(self->active_tool, 2.0);
  const double epsilon = 0.001;
  const gboolean is_small = fabs(value - small) <= epsilon;
  const gboolean is_medium = !is_small && fabs(value - medium) <= epsilon;
  const gboolean is_large = !is_small && !is_medium && fabs(value - large) <= epsilon;
  const gboolean is_custom = !is_small && !is_medium && !is_large;
  g_autofree char *value_text = g_strdup_printf("%.0f px", value);
  g_autofree char *small_tooltip = g_strdup_printf("Small (%.0f px)", small);
  g_autofree char *medium_tooltip = g_strdup_printf("Medium (%.0f px)", medium);
  g_autofree char *large_tooltip = g_strdup_printf("Large (%.0f px)", large);

  self->updating_ui = TRUE;
  gtk_toggle_button_set_active(self->small_size_button, is_small);
  gtk_toggle_button_set_active(self->medium_size_button, is_medium);
  gtk_toggle_button_set_active(self->large_size_button, is_large);
  self->updating_ui = FALSE;

  gtk_label_set_text(self->size_value_label, value_text);
  if (is_custom)
    gtk_widget_add_css_class(GTK_WIDGET(self->size_value_label), "custom");
  else
    gtk_widget_remove_css_class(GTK_WIDGET(self->size_value_label), "custom");
  gtk_widget_set_tooltip_text(GTK_WIDGET(self->small_size_button), small_tooltip);
  gtk_widget_set_tooltip_text(GTK_WIDGET(self->medium_size_button), medium_tooltip);
  gtk_widget_set_tooltip_text(GTK_WIDGET(self->large_size_button), large_tooltip);
}

static void
swash_window_set_tool_size(SwashWindow *self,
                              double          size)
{
  const gboolean is_text_tool = self->active_tool == SWASH_TOOL_TEXT;
  const double minimum = is_text_tool ? 8.0 : 1.0;
  const double maximum = is_text_tool ? 200.0 : 100.0;
  const double clamped = CLAMP(size, minimum, maximum);

  if (fabs(clamped - self->tool_widths[self->active_tool]) < 0.001)
    return;

  self->tool_widths[self->active_tool] = clamped;
  swash_window_update_size_controls(self);
  swash_window_update_canvas_cursor(self);
  gtk_widget_queue_draw(GTK_WIDGET(self->drawing_area));
}

void
swash_window_adjust_tool_size(SwashWindow *self,
                                 double          delta)
{
  if (!swash_tool_has_size_control(self->active_tool))
    return;

  swash_window_set_tool_size(self,
                                round(self->tool_widths[self->active_tool]) + delta);
}

static void
swash_window_size_preset_toggled(GtkToggleButton *button,
                                    gpointer         user_data)
{
  SwashWindow *self = SWASH_WINDOW(user_data);
  double multiplier = 1.0;

  if (self->updating_ui || !gtk_toggle_button_get_active(button))
    return;

  if (button == self->small_size_button)
    multiplier = 0.5;
  else if (button == self->large_size_button)
    multiplier = 2.0;

  swash_window_set_tool_size(self,
                                swash_tool_size_preset(self->active_tool, multiplier));
}

static gboolean
swash_window_size_segment_scroll(GtkEventControllerScroll *controller,
                                    double                    dx,
                                    double                    dy,
                                    gpointer                  user_data)
{
  SwashWindow *self = SWASH_WINDOW(user_data);
  const double delta = fabs(dx) > fabs(dy) ? dx : dy;

  (void) controller;

  if (delta == 0.0)
    return FALSE;

  swash_window_adjust_tool_size(self, delta < 0.0 ? 1.0 : -1.0);

  return TRUE;
}

static void
swash_window_undo_clicked(GtkButton *button,
                             gpointer   user_data)
{
  SwashWindow *self = SWASH_WINDOW(user_data);
  GPtrArray *snapshot;

  (void) button;

  if (self->text_editing)
    swash_window_text_editing_commit(self);
  self->selected_stroke = NULL;

  if (!swash_document_can_undo(self->document))
    return;

  snapshot = swash_document_undo(self->document);
  swash_window_restore_strokes(self, snapshot);
}

static void
swash_window_redo_clicked(GtkButton *button,
                             gpointer   user_data)
{
  SwashWindow *self = SWASH_WINDOW(user_data);
  GPtrArray *snapshot;

  (void) button;

  if (self->text_editing)
    swash_window_text_editing_commit(self);
  self->selected_stroke = NULL;

  if (!swash_document_can_redo(self->document))
    return;

  snapshot = swash_document_redo(self->document);
  swash_window_restore_strokes(self, snapshot);
}

static void
swash_window_undo_action(GtkWidget   *widget,
                            const char  *action_name,
                            GVariant    *parameter)
{
  (void) action_name;
  (void) parameter;

  swash_window_undo_clicked(NULL, widget);
}

static void
swash_window_redo_action(GtkWidget   *widget,
                            const char  *action_name,
                            GVariant    *parameter)
{
  (void) action_name;
  (void) parameter;

  swash_window_redo_clicked(NULL, widget);
}

static void
swash_window_tool_toggled(GtkToggleButton *button,
                             gpointer         user_data)
{
  SwashWindow *self = SWASH_WINDOW(user_data);

  if (!gtk_toggle_button_get_active(button))
    return;

  if (self->active_tool == SWASH_TOOL_CROP && button != self->crop_tool_button)
    swash_window_cancel_pending_crop(self);

  if (self->text_editing)
    swash_window_text_editing_commit(self);

  if (button == self->pan_tool_button)
    self->active_tool = SWASH_TOOL_PAN;
  else if (button == self->crop_tool_button)
    self->active_tool = SWASH_TOOL_CROP;
  else if (button == self->highlighter_tool_button)
    self->active_tool = SWASH_TOOL_MARKER;
  else if (button == self->eraser_tool_button)
    self->active_tool = SWASH_TOOL_ERASER;
  else if (button == self->rectangle_tool_button)
    self->active_tool = SWASH_TOOL_RECTANGLE;
  else if (button == self->circle_tool_button)
    self->active_tool = SWASH_TOOL_CIRCLE;
  else if (button == self->line_tool_button)
    self->active_tool = SWASH_TOOL_LINE;
  else if (button == self->arrow_tool_button)
    self->active_tool = SWASH_TOOL_ARROW;
  else if (button == self->ocr_tool_button)
    self->active_tool = SWASH_TOOL_OCR;
  else if (button == self->text_tool_button)
    self->active_tool = SWASH_TOOL_TEXT;
  else if (button == self->blur_tool_button)
    self->active_tool = SWASH_TOOL_BLUR;
  else if (button == self->numbering_tool_button)
    self->active_tool = SWASH_TOOL_NUMBERING;
  else if (button == self->move_tool_button)
    self->active_tool = SWASH_TOOL_MOVE;
  else
    self->active_tool = SWASH_TOOL_BRUSH;

  self->updating_ui = TRUE;
  gtk_color_dialog_button_set_rgba(self->color_button, &self->tool_colors[self->active_tool]);
  gtk_color_dialog_button_set_rgba(self->fill_color_button, &self->tool_fill_colors[self->active_tool]);
  if (self->active_tool == SWASH_TOOL_BLUR)
    gtk_drop_down_set_selected(self->blur_type_dropdown, self->blur_type);
  self->updating_ui = FALSE;

  swash_window_update_size_controls(self);
  swash_window_update_tool_ui(self);
}

static void
swash_window_color_changed(GObject    *object,
                              GParamSpec *pspec,
                              gpointer    user_data)
{
  SwashWindow *self = SWASH_WINDOW(user_data);

  (void) object;
  (void) pspec;

  if (!self->updating_ui) {
    const GdkRGBA *rgba = gtk_color_dialog_button_get_rgba(self->color_button);

    if (rgba != NULL) {
      self->tool_colors[self->active_tool] = *rgba;
      swash_window_update_canvas_cursor(self);
      gtk_widget_queue_draw(GTK_WIDGET(self->drawing_area));
    }
  }
}

static void
swash_window_fill_color_changed(GObject    *object,
                                   GParamSpec *pspec,
                                   gpointer    user_data)
{
  SwashWindow *self = SWASH_WINDOW(user_data);

  (void) object;
  (void) pspec;

  if (!self->updating_ui) {
    const GdkRGBA *rgba = gtk_color_dialog_button_get_rgba(self->fill_color_button);

    if (rgba != NULL) {
      self->tool_fill_colors[self->active_tool] = *rgba;
      gtk_widget_queue_draw(GTK_WIDGET(self->drawing_area));
    }
  }
}

static void
swash_window_blur_type_changed(GtkDropDown *dropdown,
                                  GParamSpec  *pspec,
                                  gpointer     user_data)
{
  SwashWindow *self = SWASH_WINDOW(user_data);

  (void) pspec;

  if (!self->updating_ui) {
    self->blur_type = gtk_drop_down_get_selected(dropdown);
    gtk_widget_queue_draw(GTK_WIDGET(self->drawing_area));
  }
}

static void
swash_window_connect_tool_toggle(SwashWindow  *self,
                                    GtkToggleButton *button)
{
  g_signal_connect(button, "toggled", G_CALLBACK(swash_window_tool_toggled), self);
}

void
swash_window_restore_strokes(SwashWindow *self,
                                GPtrArray      *strokes)
{
  swash_strokes_renumber(strokes);
  swash_window_refresh_document_state(self);
  swash_window_maybe_auto_copy_latest_change(self);
}

void
swash_window_update_tool_ui(SwashWindow *self)
{
  const gboolean is_pan_tool = self->active_tool == SWASH_TOOL_PAN;
  const gboolean is_crop_tool = self->active_tool == SWASH_TOOL_CROP;
  const gboolean is_ocr_tool = self->active_tool == SWASH_TOOL_OCR;
  const gboolean is_move_tool = self->active_tool == SWASH_TOOL_MOVE;
  const gboolean has_fill_color = self->active_tool == SWASH_TOOL_RECTANGLE
                               || self->active_tool == SWASH_TOOL_CIRCLE;
  gtk_widget_set_visible(self->settings_group,
                         self->texture != NULL && !is_pan_tool && !is_crop_tool && !is_ocr_tool && !is_move_tool);
  gtk_widget_set_visible(GTK_WIDGET(self->color_button),
                          !is_pan_tool &&
                          !is_crop_tool &&
                          !is_ocr_tool &&
                          self->active_tool != SWASH_TOOL_ERASER &&
                          self->active_tool != SWASH_TOOL_BLUR);
  gtk_widget_set_visible(GTK_WIDGET(self->fill_color_button), has_fill_color);
  gtk_widget_set_visible(self->size_segment_box,
                         swash_tool_has_size_control(self->active_tool));
  gtk_widget_set_visible(GTK_WIDGET(self->blur_type_dropdown),
                         self->active_tool == SWASH_TOOL_BLUR);

  if (is_ocr_tool)
    swash_window_maybe_start_ocr(self);
  swash_window_update_ocr_overlay(self);
  swash_window_update_ocr_panel(self);
  swash_window_update_crop_controls(self);
  swash_window_update_canvas_cursor(self);
}

void
swash_window_install_history_actions(GtkWidgetClass *widget_class)
{
  gtk_widget_class_install_action(widget_class, "win.undo", NULL, swash_window_undo_action);
  gtk_widget_class_install_action(widget_class, "win.redo", NULL, swash_window_redo_action);
}

void
swash_window_setup_tool_signals(SwashWindow *self)
{
  swash_window_connect_tool_toggle(self, self->pan_tool_button);
  swash_window_connect_tool_toggle(self, self->crop_tool_button);
  swash_window_connect_tool_toggle(self, self->brush_tool_button);
  swash_window_connect_tool_toggle(self, self->highlighter_tool_button);
  swash_window_connect_tool_toggle(self, self->eraser_tool_button);
  swash_window_connect_tool_toggle(self, self->rectangle_tool_button);
  swash_window_connect_tool_toggle(self, self->circle_tool_button);
  swash_window_connect_tool_toggle(self, self->line_tool_button);
  swash_window_connect_tool_toggle(self, self->arrow_tool_button);
  swash_window_connect_tool_toggle(self, self->ocr_tool_button);
  swash_window_connect_tool_toggle(self, self->text_tool_button);
  swash_window_connect_tool_toggle(self, self->blur_tool_button);
  swash_window_connect_tool_toggle(self, self->numbering_tool_button);
  swash_window_connect_tool_toggle(self, self->move_tool_button);

  g_signal_connect(self->undo_button, "clicked", G_CALLBACK(swash_window_undo_clicked), self);
  g_signal_connect(self->redo_button, "clicked", G_CALLBACK(swash_window_redo_clicked), self);

  g_signal_connect(self->small_size_button, "toggled", G_CALLBACK(swash_window_size_preset_toggled), self);
  g_signal_connect(self->medium_size_button, "toggled", G_CALLBACK(swash_window_size_preset_toggled), self);
  g_signal_connect(self->large_size_button, "toggled", G_CALLBACK(swash_window_size_preset_toggled), self);
  {
    GtkEventController *size_scroll =
      gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES
                                      | GTK_EVENT_CONTROLLER_SCROLL_DISCRETE);

    g_signal_connect(size_scroll, "scroll", G_CALLBACK(swash_window_size_segment_scroll), self);
    gtk_widget_add_controller(self->size_segment_box, size_scroll);
  }
  g_signal_connect(self->blur_type_dropdown, "notify::selected", G_CALLBACK(swash_window_blur_type_changed), self);
  g_signal_connect(self->color_button, "notify::rgba", G_CALLBACK(swash_window_color_changed), self);
  g_signal_connect(self->fill_color_button, "notify::rgba", G_CALLBACK(swash_window_fill_color_changed), self);
}
