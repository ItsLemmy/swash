#include "window-private.h"

#include <math.h>

#define SWASH_MIN_ZOOM 0.10
#define SWASH_MAX_ZOOM 8.00

static void
swash_window_get_viewport_size(SwashWindow *self,
                                  double         *width,
                                  double         *height)
{
  GtkAdjustment *hadjustment = gtk_scrolled_window_get_hadjustment(self->canvas_scroller);
  GtkAdjustment *vadjustment = gtk_scrolled_window_get_vadjustment(self->canvas_scroller);

  *width = gtk_adjustment_get_page_size(hadjustment);
  *height = gtk_adjustment_get_page_size(vadjustment);

  if (*width <= 0)
    *width = gtk_widget_get_width(GTK_WIDGET(self->canvas_scroller));

  if (*height <= 0)
    *height = gtk_widget_get_height(GTK_WIDGET(self->canvas_scroller));
}

static double
swash_window_get_fit_zoom(SwashWindow *self)
{
  double viewport_width;
  double viewport_height;
  const int texture_width = self->texture != NULL
                          ? gdk_paintable_get_intrinsic_width(GDK_PAINTABLE(self->texture))
                          : 0;
  const int texture_height = self->texture != NULL
                           ? gdk_paintable_get_intrinsic_height(GDK_PAINTABLE(self->texture))
                           : 0;

  if (texture_width <= 0 || texture_height <= 0)
    return 1.0;

  swash_window_get_viewport_size(self, &viewport_width, &viewport_height);
  if (viewport_width <= 0 || viewport_height <= 0)
    return 1.0;

  return MIN(1.0,
             MIN(viewport_width / texture_width,
                 viewport_height / texture_height));
}

void
swash_window_update_zoom_label(SwashWindow *self)
{
  if (self->texture != NULL && self->fit_mode)
    gtk_widget_add_css_class(GTK_WIDGET(self->fit_zoom_button), "selected-tool");
  else
    gtk_widget_remove_css_class(GTK_WIDGET(self->fit_zoom_button), "selected-tool");

  if (self->texture == NULL) {
    gtk_label_set_text(self->zoom_label, "--");
    return;
  }

  g_autofree char *label = g_strdup_printf("%.0f%%", swash_window_get_effective_zoom(self) * 100.0);
  gtk_label_set_text(self->zoom_label, label);
}

static gboolean
swash_window_fit_zoom_idle(gpointer user_data)
{
  SwashWindow *self = SWASH_WINDOW(user_data);
  double viewport_width;
  double viewport_height;

  if (self->texture == NULL)
    goto done;

  swash_window_get_viewport_size(self, &viewport_width, &viewport_height);
  if (viewport_width <= 0 || viewport_height <= 0)
    return G_SOURCE_CONTINUE;

  self->fit_mode = TRUE;
  self->zoom = swash_window_get_fit_zoom(self);
  swash_window_apply_zoom_mode(self);
  swash_window_update_zoom_label(self);

done:
  g_object_unref(self);
  return G_SOURCE_REMOVE;
}

static void
swash_window_canvas_size_changed(GObject    *object,
                                    GParamSpec *pspec,
                                    gpointer    user_data)
{
  SwashWindow *self = SWASH_WINDOW(user_data);

  (void) object;
  (void) pspec;

  if (self->texture == NULL || !self->fit_mode)
    return;

  self->zoom = swash_window_get_fit_zoom(self);
  swash_window_apply_zoom_mode(self);
  swash_window_update_zoom_label(self);
  swash_window_update_ocr_overlay(self);
}

static void
swash_window_viewport_changed(GObject    *object,
                                 GParamSpec *pspec,
                                 gpointer    user_data)
{
  swash_window_canvas_size_changed(object, pspec, user_data);
}

gboolean
swash_window_get_display_rect(SwashWindow *self,
                                 double          widget_width,
                                 double          widget_height,
                                 double         *display_x,
                                 double         *display_y,
                                 double         *display_width,
                                 double         *display_height)
{
  const int image_width = self->texture != NULL
                        ? gdk_paintable_get_intrinsic_width(GDK_PAINTABLE(self->texture))
                        : 0;
  const int image_height = self->texture != NULL
                         ? gdk_paintable_get_intrinsic_height(GDK_PAINTABLE(self->texture))
                         : 0;
  const double zoom = swash_window_get_effective_zoom(self);

  if (widget_width <= 0 || widget_height <= 0 || image_width <= 0 || image_height <= 0)
    return FALSE;

  *display_width = MIN(widget_width, image_width * zoom);
  *display_height = MIN(widget_height, image_height * zoom);
  *display_x = MAX(0.0, (widget_width - *display_width) / 2.0);
  *display_y = MAX(0.0, (widget_height - *display_height) / 2.0);

  return TRUE;
}

gboolean
swash_window_get_image_point(SwashWindow *self,
                                double          widget_x,
                                double          widget_y,
                                gboolean        clamp_to_image,
                                double         *image_x,
                                double         *image_y)
{
  const double widget_width = gtk_widget_get_width(GTK_WIDGET(self->drawing_area));
  const double widget_height = gtk_widget_get_height(GTK_WIDGET(self->drawing_area));
  const int image_width = self->texture != NULL
                        ? gdk_paintable_get_intrinsic_width(GDK_PAINTABLE(self->texture))
                        : 0;
  const int image_height = self->texture != NULL
                         ? gdk_paintable_get_intrinsic_height(GDK_PAINTABLE(self->texture))
                         : 0;
  double display_x;
  double display_y;
  double display_width;
  double display_height;

  if (!swash_window_get_display_rect(self,
                                        widget_width,
                                        widget_height,
                                        &display_x,
                                        &display_y,
                                        &display_width,
                                        &display_height)
      || image_width <= 0
      || image_height <= 0)
    return FALSE;

  if (!clamp_to_image
      && (widget_x < display_x
          || widget_x > display_x + display_width
          || widget_y < display_y
          || widget_y > display_y + display_height))
    return FALSE;

  widget_x = CLAMP(widget_x, display_x, display_x + display_width);
  widget_y = CLAMP(widget_y, display_y, display_y + display_height);

  *image_x = (widget_x - display_x) * image_width / display_width;
  *image_y = (widget_y - display_y) * image_height / display_height;
  return TRUE;
}

void
swash_window_set_adjustment_clamped(GtkAdjustment *adjustment,
                                       double         value)
{
  const double lower = gtk_adjustment_get_lower(adjustment);
  const double upper = MAX(lower, gtk_adjustment_get_upper(adjustment) - gtk_adjustment_get_page_size(adjustment));

  gtk_adjustment_set_value(adjustment, CLAMP(value, lower, upper));
}

gboolean
swash_window_get_pointer_viewport_position(SwashWindow *self,
                                              double         *x,
                                              double         *y)
{
  GtkAdjustment *hadjustment;
  GtkAdjustment *vadjustment;

  if (!self->pointer_in)
    return FALSE;

  hadjustment = gtk_scrolled_window_get_hadjustment(self->canvas_scroller);
  vadjustment = gtk_scrolled_window_get_vadjustment(self->canvas_scroller);
  *x = self->pointer_widget_x - gtk_adjustment_get_value(hadjustment);
  *y = self->pointer_widget_y - gtk_adjustment_get_value(vadjustment);
  return TRUE;
}

void
swash_window_get_viewport_center(SwashWindow *self,
                                    double         *x,
                                    double         *y)
{
  double width;
  double height;

  swash_window_get_viewport_size(self, &width, &height);
  *x = width / 2.0;
  *y = height / 2.0;
}

double
swash_window_get_effective_zoom(SwashWindow *self)
{
  if (self->texture == NULL)
    return 1.0;

  return self->fit_mode ? swash_window_get_fit_zoom(self) : self->zoom;
}

void
swash_window_apply_zoom_mode(SwashWindow *self)
{
  if (self->texture == NULL) {
    gtk_picture_set_can_shrink(self->picture, TRUE);
    gtk_widget_set_size_request(self->canvas_surface, -1, -1);
    swash_window_update_ocr_overlay(self);
    return;
  }

  if (self->fit_mode) {
    GtkAdjustment *hadjustment = gtk_scrolled_window_get_hadjustment(self->canvas_scroller);
    GtkAdjustment *vadjustment = gtk_scrolled_window_get_vadjustment(self->canvas_scroller);

    swash_window_set_adjustment_clamped(hadjustment, 0.0);
    swash_window_set_adjustment_clamped(vadjustment, 0.0);
  }

  gtk_picture_set_can_shrink(self->picture, TRUE);
  swash_window_update_picture_size(self);
  swash_window_update_ocr_overlay(self);
}

void
swash_window_update_picture_size(SwashWindow *self)
{
  if (self->texture == NULL) {
    gtk_widget_set_size_request(self->canvas_surface, -1, -1);
    swash_window_update_ocr_overlay(self);
    return;
  }

  const int width = gdk_paintable_get_intrinsic_width(GDK_PAINTABLE(self->texture));
  const int height = gdk_paintable_get_intrinsic_height(GDK_PAINTABLE(self->texture));
  const double zoom = swash_window_get_effective_zoom(self);
  const int req_width = MAX(1, (int) lround(width * zoom));
  const int req_height = MAX(1, (int) lround(height * zoom));

  gtk_widget_set_size_request(self->canvas_surface, req_width, req_height);
  swash_window_update_ocr_overlay(self);
  swash_window_update_canvas_cursor(self);
}

void
swash_window_set_zoom_at(SwashWindow *self,
                            double          zoom,
                            double          viewport_x,
                            double          viewport_y)
{
  GtkAdjustment *hadjustment;
  GtkAdjustment *vadjustment;
  const double fit_zoom = swash_window_get_fit_zoom(self);

  if (self->texture == NULL)
    return;

  zoom = CLAMP(zoom, SWASH_MIN_ZOOM, SWASH_MAX_ZOOM);

  if (self->fit_mode && fabs(zoom - fit_zoom) < 0.0001)
    return;

  if (!self->fit_mode && fabs(zoom - self->zoom) < 0.0001)
    return;

  hadjustment = gtk_scrolled_window_get_hadjustment(self->canvas_scroller);
  vadjustment = gtk_scrolled_window_get_vadjustment(self->canvas_scroller);

  double old_scroll_x = gtk_adjustment_get_value(hadjustment);
  double old_scroll_y = gtk_adjustment_get_value(vadjustment);

  double old_widget_x = old_scroll_x + viewport_x;
  double old_widget_y = old_scroll_y + viewport_y;

  double widget_width = gtk_widget_get_width(self->canvas_surface);
  double widget_height = gtk_widget_get_height(self->canvas_surface);

  double old_display_x, old_display_y, old_display_width, old_display_height;
  if (!swash_window_get_display_rect(self, widget_width, widget_height,
                                        &old_display_x, &old_display_y,
                                        &old_display_width, &old_display_height)) {
    old_display_x = old_display_y = 0;
    old_display_width = widget_width;
    old_display_height = widget_height;
  }

  double img_rel_x = old_display_width > 0 ? (old_widget_x - old_display_x) / old_display_width : 0.0;
  double img_rel_y = old_display_height > 0 ? (old_widget_y - old_display_y) / old_display_height : 0.0;

  self->fit_mode = FALSE;
  self->zoom = zoom;
  swash_window_apply_zoom_mode(self);
  swash_window_update_zoom_label(self);

  double viewport_width, viewport_height;
  swash_window_get_viewport_size(self, &viewport_width, &viewport_height);

  const int image_width = gdk_paintable_get_intrinsic_width(GDK_PAINTABLE(self->texture));
  const int image_height = gdk_paintable_get_intrinsic_height(GDK_PAINTABLE(self->texture));

  double new_display_width = image_width * zoom;
  double new_display_height = image_height * zoom;

  double new_widget_width = MAX(viewport_width, new_display_width);
  double new_widget_height = MAX(viewport_height, new_display_height);

  double new_display_x = MAX(0.0, (new_widget_width - new_display_width) / 2.0);
  double new_display_y = MAX(0.0, (new_widget_height - new_display_height) / 2.0);

  double new_widget_x = new_display_x + img_rel_x * new_display_width;
  double new_widget_y = new_display_y + img_rel_y * new_display_height;

  double new_scroll_x = new_widget_x - viewport_x;
  double new_scroll_y = new_widget_y - viewport_y;

  swash_window_set_adjustment_clamped(hadjustment, new_scroll_x);
  swash_window_set_adjustment_clamped(vadjustment, new_scroll_y);
}

void
swash_window_queue_fit_zoom(SwashWindow *self)
{
  g_idle_add(swash_window_fit_zoom_idle, g_object_ref(self));
}

void
swash_window_sync_state(SwashWindow *self)
{
  const gboolean has_image = self->texture != NULL;
  const gboolean has_current_file = self->current_file != NULL;
  g_autoptr(GFile) parent = has_current_file ? g_file_get_parent(self->current_file) : NULL;

  gtk_stack_set_visible_child(self->canvas_stack,
                              has_image ? GTK_WIDGET(self->canvas_scroller) : self->empty_page);
  gtk_widget_set_visible(self->open_actions, has_image);
  gtk_widget_set_visible(self->tool_group, has_image);
  gtk_widget_set_visible(self->history_actions, has_image);
  gtk_widget_set_visible(self->document_actions, has_image);
  gtk_widget_set_visible(self->file_group, has_image);
  gtk_widget_set_visible(self->zoom_group, has_image);
  gtk_widget_set_visible(self->settings_group, has_image);
  gtk_widget_set_sensitive(self->tool_group, has_image);
  gtk_widget_set_sensitive(self->settings_group, has_image);
  gtk_widget_set_sensitive(GTK_WIDGET(self->file_button), has_current_file);
  gtk_widget_set_sensitive(GTK_WIDGET(self->copy_button), has_image);
  gtk_widget_set_sensitive(self->zoom_group, has_image);
  gtk_widget_action_set_enabled(GTK_WIDGET(self), "win.open-current-file", has_current_file);
  gtk_widget_action_set_enabled(GTK_WIDGET(self), "win.open-containing-folder", parent != NULL);
  swash_window_reset_save_button(self);
  swash_window_update_history_buttons(self);
  swash_window_update_ocr_panel(self);

  if (!has_image) {
    self->zoom = 1.0;
    self->fit_mode = TRUE;
    self->drawing = FALSE;
    gtk_label_set_text(self->file_label, "no image loaded");
    swash_window_update_zoom_label(self);
    swash_window_update_tool_ui(self);
    return;
  }

  swash_window_update_zoom_label(self);
  swash_window_update_tool_ui(self);
}

void
swash_window_setup_signals(SwashWindow *self)
{
  GtkAdjustment *hadjustment = gtk_scrolled_window_get_hadjustment(self->canvas_scroller);
  GtkAdjustment *vadjustment = gtk_scrolled_window_get_vadjustment(self->canvas_scroller);

  g_signal_connect(self->canvas_scroller, "notify::width", G_CALLBACK(swash_window_canvas_size_changed), self);
  g_signal_connect(self->canvas_scroller, "notify::height", G_CALLBACK(swash_window_canvas_size_changed), self);
  g_signal_connect(hadjustment, "notify::page-size", G_CALLBACK(swash_window_viewport_changed), self);
  g_signal_connect(vadjustment, "notify::page-size", G_CALLBACK(swash_window_viewport_changed), self);
  swash_window_setup_tool_signals(self);
}
