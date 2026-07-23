#include "window-private.h"

#include "render.h"
#include "stroke.h"

#include <math.h>

typedef struct {
  GtkWidget parent_instance;
  SwashWindow *window;
} SwashStrokeOverlay;

typedef struct {
  GtkWidgetClass parent_class;
} SwashStrokeOverlayClass;

#define SWASH_TYPE_STROKE_OVERLAY (swash_stroke_overlay_get_type())

G_DEFINE_FINAL_TYPE(SwashStrokeOverlay, swash_stroke_overlay, GTK_TYPE_WIDGET)

static void
swash_window_draw_eraser_dual_ring(cairo_t *cr,
                                      double   x,
                                      double   y,
                                      double   radius,
                                      double   scale)
{
  const double outer_width = 2.5 / scale;
  const double inner_width = 1.25 / scale;

  cairo_arc(cr, x, y, radius, 0.0, 2.0 * G_PI);
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.95);
  cairo_set_line_width(cr, outer_width);
  cairo_stroke_preserve(cr);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.95);
  cairo_set_line_width(cr, inner_width);
  cairo_stroke(cr);
}

void
swash_window_update_canvas_cursor(SwashWindow *self)
{
  g_autoptr(GBytes) bytes = NULL;
  g_autoptr(GdkTexture) texture = NULL;
  g_autoptr(GdkCursor) cursor = NULL;
  cairo_surface_t *surface;
  cairo_t *cr;
  GdkRGBA color;
  double diameter;
  int size;
  int stride;

  if (self->active_tool != SWASH_TOOL_BRUSH
      && self->active_tool != SWASH_TOOL_MARKER
      && self->active_tool != SWASH_TOOL_ERASER) {
    gtk_widget_set_cursor(GTK_WIDGET(self->drawing_area), NULL);
    return;
  }

  diameter = CLAMP(self->tool_widths[self->active_tool]
                   * swash_window_get_effective_zoom(self),
                   1.0,
                   120.0);
  size = MAX(9, (int) ceil(diameter + 8.0));
  surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
  cr = cairo_create(surface);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

  color = self->tool_colors[self->active_tool];
  if (self->active_tool == SWASH_TOOL_ERASER) {
    swash_window_draw_eraser_dual_ring(cr,
                                       size / 2.0,
                                       size / 2.0,
                                       diameter / 2.0,
                                       1.0);
  } else if (self->active_tool == SWASH_TOOL_MARKER) {
    cairo_rectangle(cr,
                    size / 2.0 - diameter / 4.0,
                    size / 2.0 - diameter / 2.0,
                    diameter / 2.0,
                    diameter);
    cairo_set_source_rgba(cr, color.red, color.green, color.blue, 0.45);
    cairo_fill(cr);
  } else {
    cairo_arc(cr, size / 2.0, size / 2.0, diameter / 2.0, 0.0, 2.0 * G_PI);
    cairo_set_source_rgba(cr, color.red, color.green, color.blue, color.alpha);
    cairo_fill(cr);
  }

  cairo_destroy(cr);
  cairo_surface_flush(surface);
  stride = cairo_image_surface_get_stride(surface);
  bytes = g_bytes_new(cairo_image_surface_get_data(surface), (gsize) stride * size);
  texture = gdk_memory_texture_new(size, size, GDK_MEMORY_DEFAULT, bytes, stride);
  cursor = gdk_cursor_new_from_texture(texture, size / 2, size / 2, NULL);
  gtk_widget_set_cursor(GTK_WIDGET(self->drawing_area), cursor);
  cairo_surface_destroy(surface);
}

static void
swash_window_ensure_annotation_cache(SwashWindow *self,
                                     int          width,
                                     int          height)
{
  const guint image_generation =
    swash_document_get_image_generation(self->document);
  const guint annotations_generation =
    swash_document_get_annotations_generation(self->document);
  /* Only the move-tool exclusion participates in the cache key: a stroke
   * being moved appears in caches built before the drag, so those become
   * invalid. The in-progress stroke never does (it is added without bumping
   * the annotations generation), so excluding it needs no rebuild. */
  SwashStroke *excluded = (self->drawing && self->active_tool == SWASH_TOOL_MOVE)
                        ? self->selected_stroke
                        : NULL;
  cairo_t *cache_cr;

  if (self->annotation_cache != NULL
      && self->annotation_cache_width == width
      && self->annotation_cache_height == height
      && self->annotation_cache_image_generation == image_generation
      && self->annotation_cache_generation == annotations_generation
      && self->annotation_cache_allow_marker_overlap == self->allow_highlighter_overlap
      && self->annotation_cache_excluded == excluded)
    return;

  if (self->annotation_cache != NULL)
    cairo_surface_destroy(self->annotation_cache);

  self->annotation_cache =
    cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  self->annotation_cache_width = width;
  self->annotation_cache_height = height;
  self->annotation_cache_image_generation = image_generation;
  self->annotation_cache_generation = annotations_generation;
  self->annotation_cache_allow_marker_overlap = self->allow_highlighter_overlap;
  self->annotation_cache_excluded = excluded;
  cache_cr = cairo_create(self->annotation_cache);
  cairo_set_operator(cache_cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cache_cr);
  cairo_set_operator(cache_cr, CAIRO_OPERATOR_OVER);
  swash_render_strokes_except(cache_cr,
                              swash_window_strokes(self),
                              self->image_surface,
                              self->allow_highlighter_overlap,
                              swash_stroke_render,
                              image_generation,
                              self->current_stroke != NULL ? self->current_stroke
                                                           : excluded);

  cairo_destroy(cache_cr);
}

void
swash_window_annotation_cache_append_stroke(SwashWindow *self,
                                            SwashStroke *stroke)
{
  const guint image_generation =
    swash_document_get_image_generation(self->document);
  const guint annotations_generation =
    swash_document_get_annotations_generation(self->document);
  GPtrArray *strokes = swash_window_strokes(self);
  cairo_t *cache_cr;

  if (self->annotation_cache == NULL
      || stroke == NULL
      || strokes == NULL
      || self->image_surface == NULL)
    return;

  if (self->annotation_cache_generation + 1 != annotations_generation
      || self->annotation_cache_image_generation != image_generation
      || self->annotation_cache_width != cairo_image_surface_get_width(self->image_surface)
      || self->annotation_cache_height != cairo_image_surface_get_height(self->image_surface)
      || self->annotation_cache_allow_marker_overlap != self->allow_highlighter_overlap)
    return;

  if (self->annotation_cache_excluded != NULL
      && self->annotation_cache_excluded != stroke)
    return;

  if (!self->allow_highlighter_overlap) {
    for (guint i = 0; i < strokes->len; i++) {
      const SwashStroke *existing = g_ptr_array_index(strokes, i);

      if (existing->tool == SWASH_TOOL_MARKER)
        return;
    }
  }

  cache_cr = cairo_create(self->annotation_cache);
  swash_stroke_render(cache_cr, stroke, self->image_surface, image_generation);
  cairo_destroy(cache_cr);
  self->annotation_cache_generation = annotations_generation;
  self->annotation_cache_excluded = NULL;
}

void
swash_window_update_active_stroke_overlay(SwashWindow *self)
{
  if (self->active_stroke_overlay != NULL)
    gtk_widget_queue_draw(self->active_stroke_overlay);
}

/* While a brush or marker stroke is in progress, the completed part is kept
 * as a list of immutable render nodes covering fixed batches of points. The
 * snapshot re-appends the same node objects every frame, so GSK's node diff
 * limits the damage region — and therefore all CPU rasterization — to the
 * small live tail. Segments are drawn opaque with round caps (individual
 * capsules cover exactly the same area as one round-joined polyline stroke);
 * translucency is applied once around the whole stack with an opacity node,
 * so overlapping nodes never self-darken. */

#define SWASH_STROKE_BAKE_BATCH 16

void
swash_window_reset_active_stroke_nodes(SwashWindow *self)
{
  g_clear_pointer(&self->active_stroke_nodes, g_ptr_array_unref);
  self->active_stroke_baked_points = 0;
  self->active_stroke_baked_stroke = NULL;
}

static gboolean
swash_stroke_uses_incremental_nodes(const SwashStroke *stroke)
{
  return stroke->tool == SWASH_TOOL_BRUSH || stroke->tool == SWASH_TOOL_MARKER;
}

static void
swash_stroke_range_bounds(SwashStroke     *stroke,
                          guint            first,
                          guint            last,
                          graphene_rect_t *bounds)
{
  const double pad = stroke->width + 1.0;
  double min_x = G_MAXDOUBLE;
  double min_y = G_MAXDOUBLE;
  double max_x = -G_MAXDOUBLE;
  double max_y = -G_MAXDOUBLE;

  for (guint i = first; i <= last; i++) {
    const SwashPoint *p = &g_array_index(stroke->points, SwashPoint, i);

    min_x = MIN(min_x, p->x);
    min_y = MIN(min_y, p->y);
    max_x = MAX(max_x, p->x);
    max_y = MAX(max_y, p->y);
  }

  graphene_rect_init(bounds,
                     (float) (min_x - pad),
                     (float) (min_y - pad),
                     (float) (max_x - min_x + pad * 2.0),
                     (float) (max_y - min_y + pad * 2.0));
}

static void
swash_stroke_draw_marker_cap(cairo_t     *cr,
                             SwashStroke *stroke,
                             guint        at,
                             guint        towards)
{
  const SwashPoint *p = &g_array_index(stroke->points, SwashPoint, at);
  const SwashPoint *q = &g_array_index(stroke->points, SwashPoint, towards);
  const double dx = p->x - q->x;
  const double dy = p->y - q->y;

  if (hypot(dx, dy) <= 0.0001)
    return;

  cairo_save(cr);
  cairo_translate(cr, p->x, p->y);
  cairo_rotate(cr, atan2(dy, dx));
  cairo_rectangle(cr, 0.0, -stroke->width / 2.0, stroke->width / 2.0, stroke->width);
  cairo_fill(cr);
  cairo_restore(cr);
}

/* Draws points [first..last] as an opaque polyline, with the marker's square
 * start cap when the range begins the stroke. */
static void
swash_stroke_draw_point_range(cairo_t     *cr,
                              SwashStroke *stroke,
                              guint        first,
                              guint        last)
{
  const SwashPoint *start = &g_array_index(stroke->points, SwashPoint, first);

  cairo_set_source_rgba(cr, stroke->r, stroke->g, stroke->b, 1.0);

  if (first == last) {
    if (stroke->tool == SWASH_TOOL_MARKER)
      cairo_rectangle(cr,
                      start->x - stroke->width / 4.0,
                      start->y - stroke->width / 2.0,
                      stroke->width / 2.0,
                      stroke->width);
    else
      cairo_arc(cr, start->x, start->y, stroke->width / 2.0, 0.0, 2.0 * G_PI);
    cairo_fill(cr);
    return;
  }

  cairo_set_line_width(cr, stroke->width);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  cairo_move_to(cr, start->x, start->y);
  for (guint i = first + 1; i <= last; i++) {
    const SwashPoint *p = &g_array_index(stroke->points, SwashPoint, i);

    cairo_line_to(cr, p->x, p->y);
  }
  cairo_stroke(cr);

  if (stroke->tool == SWASH_TOOL_MARKER && first == 0)
    swash_stroke_draw_marker_cap(cr, stroke, 0, 1);
}

/* Bakes points [first..last] into an immutable node. The display transform
 * is applied on the baking snapshot BEFORE append_cairo so GTK flattens it
 * into the cairo node: the node then renders through exactly the same code
 * path as the drawing area's cairo node. Rendering the transform as a GSK
 * transform node instead places the rasterized texture up to a pixel off
 * under fractional scales, which made strokes visibly snap on release. */
static GskRenderNode *
swash_window_bake_stroke_batch(SwashStroke *stroke,
                               guint        first,
                               guint        last,
                               double       display_x,
                               double       display_y,
                               double       scale_x,
                               double       scale_y)
{
  GtkSnapshot *batch = gtk_snapshot_new();
  graphene_rect_t bounds;
  cairo_t *cr;

  swash_stroke_range_bounds(stroke, first, last, &bounds);
  gtk_snapshot_translate(batch,
                         &GRAPHENE_POINT_INIT((float) display_x, (float) display_y));
  gtk_snapshot_scale(batch, (float) scale_x, (float) scale_y);
  cr = gtk_snapshot_append_cairo(batch, &bounds);
  swash_stroke_draw_point_range(cr, stroke, first, last);
  cairo_destroy(cr);
  return gtk_snapshot_free_to_node(batch);
}

static void
swash_window_snapshot_active_stroke(SwashWindow *self,
                                    GtkSnapshot *snapshot,
                                    double       display_x,
                                    double       display_y,
                                    double       scale_x,
                                    double       scale_y)
{
  SwashStroke *stroke = self->current_stroke;
  const guint len = stroke->points->len;
  const double alpha = stroke->tool == SWASH_TOOL_MARKER ? 0.45 : stroke->a;
  const gboolean translucent = alpha < 0.999;
  guint tail_first;

  if (self->active_stroke_baked_stroke != stroke
      || self->active_stroke_baked_points > len
      || self->active_stroke_bake_dx != display_x
      || self->active_stroke_bake_dy != display_y
      || self->active_stroke_bake_sx != scale_x
      || self->active_stroke_bake_sy != scale_y)
    swash_window_reset_active_stroke_nodes(self);

  if (self->active_stroke_nodes == NULL)
    self->active_stroke_nodes =
      g_ptr_array_new_with_free_func((GDestroyNotify) gsk_render_node_unref);
  self->active_stroke_baked_stroke = stroke;
  self->active_stroke_bake_dx = display_x;
  self->active_stroke_bake_dy = display_y;
  self->active_stroke_bake_sx = scale_x;
  self->active_stroke_bake_sy = scale_y;

  /* Bake full batches, always keeping at least the last point out: point
   * simplification may still replace it in place. */
  while (len >= self->active_stroke_baked_points + SWASH_STROKE_BAKE_BATCH + 1) {
    const guint baked = self->active_stroke_baked_points;
    const guint first = baked > 0 ? baked - 1 : 0;
    GskRenderNode *node =
      swash_window_bake_stroke_batch(stroke,
                                     first,
                                     baked + SWASH_STROKE_BAKE_BATCH - 1,
                                     display_x,
                                     display_y,
                                     scale_x,
                                     scale_y);

    if (node != NULL)
      g_ptr_array_add(self->active_stroke_nodes, node);
    self->active_stroke_baked_points = baked + SWASH_STROKE_BAKE_BATCH;
  }

  if (translucent)
    gtk_snapshot_push_opacity(snapshot, alpha);

  for (guint i = 0; i < self->active_stroke_nodes->len; i++)
    gtk_snapshot_append_node(snapshot,
                             g_ptr_array_index(self->active_stroke_nodes, i));

  tail_first = self->active_stroke_baked_points > 0
             ? self->active_stroke_baked_points - 1
             : 0;
  gtk_snapshot_save(snapshot);
  gtk_snapshot_translate(snapshot,
                         &GRAPHENE_POINT_INIT((float) display_x, (float) display_y));
  gtk_snapshot_scale(snapshot, (float) scale_x, (float) scale_y);
  {
    graphene_rect_t tail_bounds;
    cairo_t *cr;

    swash_stroke_range_bounds(stroke, tail_first, len - 1, &tail_bounds);
    cr = gtk_snapshot_append_cairo(snapshot, &tail_bounds);
    swash_stroke_draw_point_range(cr, stroke, tail_first, len - 1);
    if (stroke->tool == SWASH_TOOL_MARKER && len >= 2)
      swash_stroke_draw_marker_cap(cr, stroke, len - 1, len - 2);
    cairo_destroy(cr);
  }
  gtk_snapshot_restore(snapshot);

  if (translucent)
    gtk_snapshot_pop(snapshot);
}

static void
swash_stroke_overlay_snapshot(GtkWidget   *widget,
                              GtkSnapshot *snapshot)
{
  SwashStrokeOverlay *overlay = (SwashStrokeOverlay *) widget;
  SwashWindow *self = overlay->window;
  const int widget_width = gtk_widget_get_width(widget);
  const int widget_height = gtk_widget_get_height(widget);
  const int image_width = self->texture != NULL ? gdk_texture_get_width(self->texture) : 0;
  const int image_height = self->texture != NULL ? gdk_texture_get_height(self->texture) : 0;
  graphene_rect_t bounds;
  cairo_t *cr;
  double display_x;
  double display_y;
  double display_width;
  double display_height;
  double bounds_x;
  double bounds_y;
  double bounds_width;
  double bounds_height;
  double scale;
  double padding;
  double left;
  double top;
  double right;
  double bottom;

  if (!self->drawing || self->current_stroke == NULL || image_width <= 0 || image_height <= 0)
    return;

  if (!swash_window_get_display_rect(self,
                                     widget_width,
                                     widget_height,
                                     &display_x,
                                     &display_y,
                                     &display_width,
                                     &display_height))
    return;

  if (swash_stroke_uses_incremental_nodes(self->current_stroke)
      && self->current_stroke->points->len >= 1) {
    swash_window_snapshot_active_stroke(self,
                                        snapshot,
                                        display_x,
                                        display_y,
                                        display_width / image_width,
                                        display_height / image_height);
    return;
  }

  swash_stroke_get_bounds(self->current_stroke,
                          &bounds_x,
                          &bounds_y,
                          &bounds_width,
                          &bounds_height);
  scale = display_width / image_width;
  padding = MAX(4.0, self->current_stroke->width * scale * 3.0);
  left = MAX(floor(display_x + bounds_x * scale - padding), 0.0);
  top = MAX(floor(display_y + bounds_y * scale - padding), 0.0);
  right = MIN(ceil(display_x + (bounds_x + bounds_width) * scale + padding),
              (double) widget_width);
  bottom = MIN(ceil(display_y + (bounds_y + bounds_height) * scale + padding),
               (double) widget_height);

  /* The overlay spans the whole zoomed canvas; only the scrolled viewport is
   * ever visible, so never rasterize beyond it. */
  {
    GtkAdjustment *hadjustment =
      gtk_scrolled_window_get_hadjustment(self->canvas_scroller);
    GtkAdjustment *vadjustment =
      gtk_scrolled_window_get_vadjustment(self->canvas_scroller);
    const double page_width = gtk_adjustment_get_page_size(hadjustment);
    const double page_height = gtk_adjustment_get_page_size(vadjustment);

    if (page_width > 0.0) {
      const double view_left = gtk_adjustment_get_value(hadjustment);

      left = MAX(left, floor(view_left));
      right = MIN(right, ceil(view_left + page_width));
    }

    if (page_height > 0.0) {
      const double view_top = gtk_adjustment_get_value(vadjustment);

      top = MAX(top, floor(view_top));
      bottom = MIN(bottom, ceil(view_top + page_height));
    }
  }

  if (right <= left || bottom <= top)
    return;

  graphene_rect_init(&bounds,
                     (float) left,
                     (float) top,
                     (float) (right - left),
                     (float) (bottom - top));
  cr = gtk_snapshot_append_cairo(snapshot, &bounds);
  cairo_translate(cr, display_x, display_y);
  cairo_scale(cr, display_width / image_width, display_height / image_height);
  swash_stroke_render(cr,
                      self->current_stroke,
                      self->image_surface,
                      swash_document_get_image_generation(self->document));
  cairo_destroy(cr);
}

static void
swash_stroke_overlay_measure(GtkWidget      *widget,
                             GtkOrientation  orientation,
                             int             for_size,
                             int            *minimum,
                             int            *natural,
                             int            *minimum_baseline,
                             int            *natural_baseline)
{
  (void) widget;
  (void) orientation;
  (void) for_size;

  *minimum = 0;
  *natural = 0;
  if (minimum_baseline != NULL)
    *minimum_baseline = -1;
  if (natural_baseline != NULL)
    *natural_baseline = -1;
}

static void
swash_stroke_overlay_class_init(SwashStrokeOverlayClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

  widget_class->measure = swash_stroke_overlay_measure;
  widget_class->snapshot = swash_stroke_overlay_snapshot;
}

static void
swash_stroke_overlay_init(SwashStrokeOverlay *overlay)
{
  (void) overlay;
}

GtkWidget *
swash_stroke_overlay_new(SwashWindow *self)
{
  SwashStrokeOverlay *overlay =
    g_object_new(SWASH_TYPE_STROKE_OVERLAY,
                 "can-target", FALSE,
                 "focusable", FALSE,
                 "hexpand", TRUE,
                 "vexpand", TRUE,
                 NULL);

  overlay->window = self;
  return GTK_WIDGET(overlay);
}

void
swash_window_drawing_area_draw(GtkDrawingArea *area,
                                  cairo_t        *cr,
                                  int             width,
                                  int             height,
                                  gpointer        user_data)
{
  SwashWindow *self = SWASH_WINDOW(user_data);
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
  GPtrArray *strokes = swash_window_strokes(self);
  (void) area;

  if (strokes == NULL || width <= 0 || height <= 0 || image_width <= 0 || image_height <= 0)
    return;

  if (!swash_window_get_display_rect(self,
                                        width,
                                        height,
                                        &display_x,
                                        &display_y,
                                        &display_width,
                                        &display_height))
    return;

  cairo_save(cr);
  cairo_rectangle(cr, display_x, display_y, display_width, display_height);
  cairo_clip(cr);
  cairo_translate(cr, display_x, display_y);
  cairo_scale(cr, display_width / image_width, display_height / image_height);
  swash_window_ensure_annotation_cache(self, image_width, image_height);
  if (self->annotation_cache != NULL) {
    cairo_set_source_surface(cr, self->annotation_cache, 0.0, 0.0);
    /* Bilinear matches the GPU sampling used for the underlying picture;
     * cairo's default GOOD filter falls back to a separable convolution on
     * downscale, which is far too slow to run on every repaint. */
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
    cairo_paint(cr);
  }
  if (!self->drawing && self->current_stroke != NULL)
    swash_stroke_render(cr,
                        self->current_stroke,
                        self->image_surface,
                        swash_document_get_image_generation(self->document));

  /* While a stroke is being moved it is excluded from the annotation cache
   * so the cache survives the drag; draw it live on top instead. */
  if (self->drawing
      && self->active_tool == SWASH_TOOL_MOVE
      && self->selected_stroke != NULL)
    swash_stroke_render(cr,
                        self->selected_stroke,
                        self->image_surface,
                        swash_document_get_image_generation(self->document));

  if (self->active_tool == SWASH_TOOL_MOVE && self->selected_stroke != NULL) {
    double bx, by, bw, bh;
    const double dash[] = { 6.0 / (display_width / image_width), 4.0 / (display_width / image_width) };

    swash_stroke_get_bounds(self->selected_stroke, &bx, &by, &bw, &bh);
    cairo_set_dash(cr, dash, G_N_ELEMENTS(dash), 0.0);
    cairo_set_line_width(cr, 1.5 / (display_width / image_width));
    cairo_rectangle(cr, bx - 2.0, by - 2.0, bw + 4.0, bh + 4.0);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.8);
    cairo_stroke_preserve(cr);
    cairo_set_dash(cr, dash, G_N_ELEMENTS(dash), dash[0]);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.8);
    cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0.0);
  }

  if (self->text_editing && self->text_cursor_visible && self->current_stroke != NULL
      && self->current_stroke->points->len >= 1) {
    const SwashPoint *p = &g_array_index(self->current_stroke->points, SwashPoint, 0);
    cairo_text_extents_t extents = {0};
    cairo_font_extents_t font_ext;
    double cursor_x;
    const double scale = display_width / image_width;

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, self->current_stroke->width);

    if (self->current_stroke->text != NULL && *self->current_stroke->text != '\0')
      cairo_text_extents(cr, self->current_stroke->text, &extents);

    cairo_font_extents(cr, &font_ext);
    cursor_x = p->x + extents.x_advance;

    cairo_set_line_width(cr, 2.0 / scale);
    cairo_set_source_rgba(cr,
                          self->current_stroke->r,
                          self->current_stroke->g,
                          self->current_stroke->b,
                          self->current_stroke->a);
    cairo_move_to(cr, cursor_x, p->y - font_ext.ascent);
    cairo_line_to(cr, cursor_x, p->y + font_ext.descent);
    cairo_stroke(cr);
  }

  cairo_restore(cr);

  if (self->active_tool == SWASH_TOOL_CROP
      && (self->crop_selection_active || self->drawing)) {
    const double crop_left = MIN(self->crop_start_x, self->crop_end_x);
    const double crop_top = MIN(self->crop_start_y, self->crop_end_y);
    const double crop_width = fabs(self->crop_end_x - self->crop_start_x);
    const double crop_height = fabs(self->crop_end_y - self->crop_start_y);
    const double rect_x = display_x + crop_left * display_width / image_width;
    const double rect_y = display_y + crop_top * display_height / image_height;
    const double rect_width = crop_width * display_width / image_width;
    const double rect_height = crop_height * display_height / image_height;
    const double dash[] = { 8.0, 6.0 };

    if (rect_width > 0.0 && rect_height > 0.0) {
      cairo_save(cr);
      cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
      cairo_rectangle(cr, display_x, display_y, display_width, display_height);
      cairo_rectangle(cr, rect_x, rect_y, rect_width, rect_height);
      cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.4);
      cairo_fill(cr);

      cairo_rectangle(cr, rect_x, rect_y, rect_width, rect_height);
      cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.95);
      cairo_set_line_width(cr, 2.0);
      cairo_set_dash(cr, dash, G_N_ELEMENTS(dash), 0.0);
      cairo_stroke(cr);

      {
        const double handle_size = 9.0;
        const double handle_x[] = {
          rect_x, rect_x + rect_width / 2.0, rect_x + rect_width,
          rect_x, rect_x + rect_width,
          rect_x, rect_x + rect_width / 2.0, rect_x + rect_width,
        };
        const double handle_y[] = {
          rect_y, rect_y, rect_y,
          rect_y + rect_height / 2.0, rect_y + rect_height / 2.0,
          rect_y + rect_height, rect_y + rect_height, rect_y + rect_height,
        };

        cairo_set_dash(cr, NULL, 0, 0.0);
        for (guint i = 0; i < G_N_ELEMENTS(handle_x); i++) {
          cairo_rectangle(cr,
                          handle_x[i] - handle_size / 2.0,
                          handle_y[i] - handle_size / 2.0,
                          handle_size,
                          handle_size);
          cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
          cairo_fill_preserve(cr);
          cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.9);
          cairo_set_line_width(cr, 1.0);
          cairo_stroke(cr);
        }
      }
      cairo_restore(cr);
    }
  }

}
