#pragma once

#include "types.h"

void swash_render_strokes(cairo_t         *cr,
                             GPtrArray       *strokes,
                             cairo_surface_t *source_surface,
                             gboolean         allow_marker_overlap,
                             SwashStrokeRenderFunc render_stroke,
                             guint            image_generation);
