#include "render.h"

void
swash_render_strokes(cairo_t                *cr,
                        GPtrArray              *strokes,
                        cairo_surface_t        *source_surface,
                        gboolean                allow_marker_overlap,
                        SwashStrokeRenderFunc render_stroke,
                        guint                   image_generation)
{
  swash_render_strokes_except(cr,
                              strokes,
                              source_surface,
                              allow_marker_overlap,
                              render_stroke,
                              image_generation,
                              NULL);
}

void
swash_render_strokes_except(cairo_t                *cr,
                            GPtrArray              *strokes,
                            cairo_surface_t        *source_surface,
                            gboolean                allow_marker_overlap,
                            SwashStrokeRenderFunc render_stroke,
                            guint                   image_generation,
                            SwashStroke            *excluded_stroke)
{
  cairo_surface_t *marker_surface = NULL;
  cairo_t *marker_cr = NULL;
  guint i;

  if (strokes == NULL)
    return;

  if (!allow_marker_overlap) {
    marker_surface = cairo_surface_create_similar_image(cairo_get_target(cr),
                                                        CAIRO_FORMAT_ARGB32,
                                                        cairo_image_surface_get_width(source_surface),
                                                        cairo_image_surface_get_height(source_surface));
    marker_cr = cairo_create(marker_surface);
    cairo_set_operator(marker_cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(marker_cr);
    cairo_set_operator(marker_cr, CAIRO_OPERATOR_SOURCE);
  }

  for (i = 0; i < strokes->len; i++) {
    SwashStroke *stroke = g_ptr_array_index(strokes, i);

    if (stroke == excluded_stroke)
      continue;

    if (!allow_marker_overlap && stroke->tool == SWASH_TOOL_MARKER)
      render_stroke(marker_cr, stroke, source_surface, image_generation);
    else
      render_stroke(cr, stroke, source_surface, image_generation);
  }

  if (marker_cr != NULL) {
    cairo_destroy(marker_cr);
    cairo_set_source_surface(cr, marker_surface, 0.0, 0.0);
    cairo_paint(cr);
    cairo_surface_destroy(marker_surface);
  }
}
