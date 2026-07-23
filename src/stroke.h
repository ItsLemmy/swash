#pragma once

#include "types.h"

double swash_tool_width(SwashTool tool);
gboolean swash_tool_is_shape(SwashTool tool);
gboolean swash_tool_is_non_drawing(SwashTool tool);

SwashStroke *swash_stroke_new(SwashTool    tool,
                                    double          width,
                                    const GdkRGBA  *color,
                                    const GdkRGBA  *fill_color,
                                    int             blur_type);
SwashStroke *swash_stroke_copy(SwashStroke *stroke);
void swash_stroke_free(SwashStroke *stroke);
void swash_stroke_add_point(SwashStroke *stroke,
                               double          x,
                               double          y);
void swash_stroke_set_last_point(SwashStroke *stroke,
                                    double          x,
                                    double          y);
void swash_stroke_render(cairo_t         *cr,
                            SwashStroke  *stroke,
                            cairo_surface_t *source_surface,
                            guint            image_generation);
gboolean swash_stroke_intersects_segment(SwashStroke *stroke,
                                            double          x0,
                                            double          y0,
                                            double          x1,
                                            double          y1,
                                            double          radius);
gboolean swash_stroke_hit_test(SwashStroke *stroke,
                                  double          x,
                                  double          y,
                                  double          tolerance);
void swash_stroke_offset(SwashStroke *stroke,
                            double          dx,
                            double          dy);
void swash_stroke_get_bounds(SwashStroke *stroke,
                                double         *x,
                                double         *y,
                                double         *w,
                                double         *h);
void swash_strokes_renumber(GPtrArray *strokes);
