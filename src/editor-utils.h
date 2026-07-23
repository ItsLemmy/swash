#pragma once

#include "types.h"

double swash_tool_size_preset(SwashTool tool,
                              double    multiplier);
void swash_crop_rect_normalize(double *left,
                               double *top,
                               double *right,
                               double *bottom);
void swash_crop_rect_dimensions(double  left,
                                double  top,
                                double  right,
                                double  bottom,
                                int    *width,
                                int    *height);
gboolean swash_accelerators_conflict(const char *left,
                                     const char *right);
gboolean swash_point_is_far_enough(const SwashPoint *from,
                                   const SwashPoint *to,
                                   double            minimum_distance);
gboolean swash_point_can_simplify(const SwashPoint *anchor,
                                  const SwashPoint *candidate,
                                  const SwashPoint *next,
                                  double            tolerance);
