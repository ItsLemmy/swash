#include "editor-utils.h"

#include "stroke.h"

#include <math.h>

double
swash_tool_size_preset(SwashTool tool,
                       double    multiplier)
{
  const double minimum = tool == SWASH_TOOL_TEXT ? 8.0 : 1.0;
  const double maximum = tool == SWASH_TOOL_TEXT ? 200.0 : 100.0;

  return CLAMP(swash_tool_width(tool) * multiplier, minimum, maximum);
}

void
swash_crop_rect_normalize(double *left,
                          double *top,
                          double *right,
                          double *bottom)
{
  const double normalized_left = MIN(*left, *right);
  const double normalized_top = MIN(*top, *bottom);
  const double normalized_right = MAX(*left, *right);
  const double normalized_bottom = MAX(*top, *bottom);

  *left = normalized_left;
  *top = normalized_top;
  *right = normalized_right;
  *bottom = normalized_bottom;
}

void
swash_crop_rect_dimensions(double  left,
                           double  top,
                           double  right,
                           double  bottom,
                           int    *width,
                           int    *height)
{
  swash_crop_rect_normalize(&left, &top, &right, &bottom);

  if (width != NULL)
    *width = MAX(0, (int) ceil(right) - (int) floor(left));
  if (height != NULL)
    *height = MAX(0, (int) ceil(bottom) - (int) floor(top));
}

gboolean
swash_accelerators_conflict(const char *left,
                            const char *right)
{
  guint left_keyval = 0;
  guint right_keyval = 0;
  GdkModifierType left_modifiers = 0;
  GdkModifierType right_modifiers = 0;

  if (left == NULL || right == NULL || *left == '\0' || *right == '\0')
    return FALSE;

  gtk_accelerator_parse(left, &left_keyval, &left_modifiers);
  gtk_accelerator_parse(right, &right_keyval, &right_modifiers);
  if (left_keyval == 0 || right_keyval == 0)
    return FALSE;

  return gdk_keyval_to_lower(left_keyval) == gdk_keyval_to_lower(right_keyval)
      && (left_modifiers & gtk_accelerator_get_default_mod_mask())
      == (right_modifiers & gtk_accelerator_get_default_mod_mask());
}

gboolean
swash_point_is_far_enough(const SwashPoint *from,
                          const SwashPoint *to,
                          double            minimum_distance)
{
  return hypot(to->x - from->x, to->y - from->y) >= minimum_distance;
}

gboolean
swash_point_can_simplify(const SwashPoint *anchor,
                         const SwashPoint *candidate,
                         const SwashPoint *next,
                         double            tolerance)
{
  const double segment_x = next->x - anchor->x;
  const double segment_y = next->y - anchor->y;
  const double segment_length = hypot(segment_x, segment_y);
  const double incoming_x = candidate->x - anchor->x;
  const double incoming_y = candidate->y - anchor->y;
  const double outgoing_x = next->x - candidate->x;
  const double outgoing_y = next->y - candidate->y;
  double deviation;

  if (segment_length <= 0.0001
      || incoming_x * outgoing_x + incoming_y * outgoing_y < 0.0)
    return FALSE;

  deviation =
    fabs(segment_x * (anchor->y - candidate->y)
         - (anchor->x - candidate->x) * segment_y) / segment_length;
  return deviation <= tolerance;
}
