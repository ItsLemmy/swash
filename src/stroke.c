#include "stroke.h"

#include <math.h>
#include <stdint.h>

static void
swash_marker_add_rect(cairo_t *cr,
                         double   center_x,
                         double   center_y,
                         double   height)
{
  const double width = height / 2.0;

  cairo_rectangle(cr,
                  center_x - width / 2.0,
                  center_y - height / 2.0,
                  width,
                  height);
}

static uint32_t
swash_blur_average_pixel(uint32_t sum_b,
                         uint32_t sum_g,
                         uint32_t sum_r,
                         uint32_t sum_a,
                         uint32_t count)
{
  return ((sum_a / count) << 24)
       | ((sum_r / count) << 16)
       | ((sum_g / count) << 8)
       | (sum_b / count);
}

static void
swash_blur_add_pixel(uint32_t  pixel,
                     uint32_t *sum_b,
                     uint32_t *sum_g,
                     uint32_t *sum_r,
                     uint32_t *sum_a)
{
  *sum_b += pixel & 0xff;
  *sum_g += (pixel >> 8) & 0xff;
  *sum_r += (pixel >> 16) & 0xff;
  *sum_a += (pixel >> 24) & 0xff;
}

static void
swash_blur_subtract_pixel(uint32_t  pixel,
                          uint32_t *sum_b,
                          uint32_t *sum_g,
                          uint32_t *sum_r,
                          uint32_t *sum_a)
{
  *sum_b -= pixel & 0xff;
  *sum_g -= (pixel >> 8) & 0xff;
  *sum_r -= (pixel >> 16) & 0xff;
  *sum_a -= (pixel >> 24) & 0xff;
}

static void
swash_box_blur_horizontal(const uint32_t *src,
                          uint32_t       *dst,
                          int             width,
                          int             height,
                          int             radius)
{
  const int sample_count = radius * 2 + 1;
  int y;

  for (y = 0; y < height; y++) {
    const uint32_t *src_row = src + (gsize) y * width;
    uint32_t *dst_row = dst + (gsize) y * width;
    uint32_t sum_b = 0;
    uint32_t sum_g = 0;
    uint32_t sum_r = 0;
    uint32_t sum_a = 0;
    int x;

    for (x = -radius; x <= radius; x++) {
      const int src_x = CLAMP(x, 0, width - 1);

      swash_blur_add_pixel(src_row[src_x], &sum_b, &sum_g, &sum_r, &sum_a);
    }

    for (x = 0; x < width; x++) {
      dst_row[x] = swash_blur_average_pixel(sum_b,
                                             sum_g,
                                             sum_r,
                                             sum_a,
                                             sample_count);

      if (x + 1 < width) {
        const int remove_x = CLAMP(x - radius, 0, width - 1);
        const int add_x = CLAMP(x + radius + 1, 0, width - 1);

        swash_blur_subtract_pixel(src_row[remove_x], &sum_b, &sum_g, &sum_r, &sum_a);
        swash_blur_add_pixel(src_row[add_x], &sum_b, &sum_g, &sum_r, &sum_a);
      }
    }
  }
}

static void
swash_box_blur_vertical(const uint32_t *src,
                        uint32_t       *dst,
                        int             width,
                        int             height,
                        int             radius)
{
  const int sample_count = radius * 2 + 1;

  for (int x = 0; x < width; x++) {
    uint32_t sum_b = 0;
    uint32_t sum_g = 0;
    uint32_t sum_r = 0;
    uint32_t sum_a = 0;
    int y;

    for (y = -radius; y <= radius; y++) {
      const int src_y = CLAMP(y, 0, height - 1);

      swash_blur_add_pixel(src[(gsize) src_y * width + x], &sum_b, &sum_g, &sum_r, &sum_a);
    }

    for (y = 0; y < height; y++) {
      dst[(gsize) y * width + x] = swash_blur_average_pixel(sum_b,
                                                            sum_g,
                                                            sum_r,
                                                            sum_a,
                                                            sample_count);

      if (y + 1 < height) {
        const int remove_y = CLAMP(y - radius, 0, height - 1);
        const int add_y = CLAMP(y + radius + 1, 0, height - 1);

        swash_blur_subtract_pixel(src[(gsize) remove_y * width + x],
                                  &sum_b,
                                  &sum_g,
                                  &sum_r,
                                  &sum_a);
        swash_blur_add_pixel(src[(gsize) add_y * width + x],
                             &sum_b,
                             &sum_g,
                             &sum_r,
                             &sum_a);
      }
    }
  }
}

static void
swash_gaussian_box_radii(double sigma,
                         int    radii[3])
{
  const int pass_count = 3;
  const double ideal_width = sqrt(12.0 * sigma * sigma / pass_count + 1.0);
  int lower_width = (int) floor(ideal_width);
  int upper_width;
  int lower_passes;

  if (lower_width % 2 == 0)
    lower_width--;
  lower_width = MAX(1, lower_width);
  upper_width = lower_width + 2;
  lower_passes = (int) round((12.0 * sigma * sigma
                              - pass_count * lower_width * lower_width
                              - 4.0 * pass_count * lower_width
                              - 3.0 * pass_count)
                             / (-4.0 * lower_width - 4.0));
  lower_passes = CLAMP(lower_passes, 0, pass_count);

  for (int i = 0; i < pass_count; i++) {
    const int width = i < lower_passes ? lower_width : upper_width;

    radii[i] = (width - 1) / 2;
  }
}

static void
swash_blur_region(const unsigned char *src_data,
                  int                  src_w,
                  int                  src_h,
                  int                  src_stride,
                  unsigned char       *dst_data,
                  int                  dst_stride,
                  int                  region_x,
                  int                  region_y,
                  int                  region_w,
                  int                  region_h,
                  double               strength)
{
  int radii[3];
  int padding;
  int work_w;
  int work_h;
  gsize pixel_count;
  uint32_t *pixels;
  uint32_t *scratch;

  swash_gaussian_box_radii(MAX(0.8, strength / 3.0), radii);
  padding = radii[0] + radii[1] + radii[2];
  work_w = region_w + padding * 2;
  work_h = region_h + padding * 2;
  pixel_count = (gsize) work_w * work_h;
  pixels = g_new(uint32_t, pixel_count);
  scratch = g_new(uint32_t, pixel_count);

  for (int y = 0; y < work_h; y++) {
    const int src_y = CLAMP(region_y - padding + y, 0, src_h - 1);
    const uint32_t *src_row = (const uint32_t *) (src_data + src_y * src_stride);
    uint32_t *work_row = pixels + (gsize) y * work_w;

    for (int x = 0; x < work_w; x++) {
      const int src_x = CLAMP(region_x - padding + x, 0, src_w - 1);

      work_row[x] = src_row[src_x];
    }
  }

  for (int i = 0; i < 3; i++) {
    swash_box_blur_horizontal(pixels, scratch, work_w, work_h, radii[i]);
    swash_box_blur_vertical(scratch, pixels, work_w, work_h, radii[i]);
  }

  for (int y = 0; y < region_h; y++) {
    const uint32_t *src_row = pixels + (gsize) (y + padding) * work_w + padding;
    uint32_t *dst_row = (uint32_t *) (dst_data + y * dst_stride);

    memcpy(dst_row, src_row, (gsize) region_w * sizeof(uint32_t));
  }

  g_free(scratch);
  g_free(pixels);
}

double
swash_tool_width(SwashTool tool)
{
  switch (tool) {
  case SWASH_TOOL_PAN:
  case SWASH_TOOL_CROP:
  case SWASH_TOOL_MOVE:
    return 0.0;
  case SWASH_TOOL_MARKER:
    return 24.0;
  case SWASH_TOOL_ERASER:
    return 28.0;
  case SWASH_TOOL_RECTANGLE:
  case SWASH_TOOL_CIRCLE:
  case SWASH_TOOL_LINE:
  case SWASH_TOOL_ARROW:
    return 6.0;
  case SWASH_TOOL_BLUR:
    return 32.0;
  case SWASH_TOOL_TEXT:
    return 24.0;
  case SWASH_TOOL_NUMBERING:
    return 32.0;
  case SWASH_TOOL_BRUSH:
  default:
    return 6.0;
  }
}

gboolean
swash_tool_is_shape(SwashTool tool)
{
  return tool == SWASH_TOOL_RECTANGLE
      || tool == SWASH_TOOL_CIRCLE
      || tool == SWASH_TOOL_LINE
      || tool == SWASH_TOOL_ARROW
      || tool == SWASH_TOOL_BLUR;
}

gboolean
swash_tool_is_non_drawing(SwashTool tool)
{
  return tool == SWASH_TOOL_PAN
      || tool == SWASH_TOOL_CROP
      || tool == SWASH_TOOL_OCR;
}

SwashStroke *
swash_stroke_new(SwashTool   tool,
                    double         width,
                    const GdkRGBA *color,
                    const GdkRGBA *fill_color,
                    int            blur_type)
{
  SwashStroke *stroke = g_new0(SwashStroke, 1);

  stroke->tool = tool;
  stroke->width = width;
  stroke->r = color->red;
  stroke->g = color->green;
  stroke->b = color->blue;
  stroke->a = color->alpha;
  if (fill_color != NULL) {
    stroke->fill_r = fill_color->red;
    stroke->fill_g = fill_color->green;
    stroke->fill_b = fill_color->blue;
    stroke->fill_a = fill_color->alpha;
  }
  stroke->blur_type = blur_type;
  stroke->points = g_array_new(FALSE, FALSE, sizeof(SwashPoint));
  return stroke;
}

SwashStroke *
swash_stroke_copy(SwashStroke *stroke)
{
  SwashStroke *copy = g_new0(SwashStroke, 1);

  copy->tool = stroke->tool;
  copy->width = stroke->width;
  copy->r = stroke->r;
  copy->g = stroke->g;
  copy->b = stroke->b;
  copy->a = stroke->a;
  copy->fill_r = stroke->fill_r;
  copy->fill_g = stroke->fill_g;
  copy->fill_b = stroke->fill_b;
  copy->fill_a = stroke->fill_a;
  copy->blur_type = stroke->blur_type;
  copy->points = g_array_sized_new(FALSE, FALSE, sizeof(SwashPoint), stroke->points->len);
  g_array_append_vals(copy->points, stroke->points->data, stroke->points->len);
  if (stroke->text != NULL)
    copy->text = g_strdup(stroke->text);
  return copy;
}

void
swash_stroke_free(SwashStroke *stroke)
{
  if (stroke == NULL)
    return;

  g_clear_pointer(&stroke->points, g_array_unref);
  if (stroke->blur_cache != NULL)
    cairo_surface_destroy(stroke->blur_cache);
  g_free(stroke->text);
  g_free(stroke);
}

void
swash_stroke_add_point(SwashStroke *stroke,
                          double          x,
                          double          y)
{
  const guint len = stroke->points->len;
  SwashPoint point = { x, y };

  if (len > 0) {
    const SwashPoint *last = &g_array_index(stroke->points, SwashPoint, len - 1);

    if (fabs(last->x - x) < 0.5 && fabs(last->y - y) < 0.5)
      return;
  }

  g_array_append_val(stroke->points, point);
}

void
swash_stroke_set_last_point(SwashStroke *stroke,
                               double          x,
                               double          y)
{
  SwashPoint point = { x, y };

  if (stroke->points->len == 0) {
    g_array_append_val(stroke->points, point);
    return;
  }

  if (stroke->points->len == 1) {
    g_array_append_val(stroke->points, point);
    return;
  }

  g_array_index(stroke->points, SwashPoint, stroke->points->len - 1) = point;
}

void
swash_stroke_render(cairo_t         *cr,
                       SwashStroke  *stroke,
                       cairo_surface_t *source_surface,
                       guint            image_generation)
{
  const guint len = stroke->points->len;
  guint i;

  if (len == 0)
    return;

  cairo_new_path(cr);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  cairo_set_line_width(cr, stroke->width);

  switch (stroke->tool) {
  case SWASH_TOOL_MARKER:
    cairo_set_source_rgba(cr, stroke->r, stroke->g, stroke->b, 0.45);
    break;
  case SWASH_TOOL_BLUR:
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.6);
    break;
  case SWASH_TOOL_TEXT:
    cairo_set_source_rgba(cr, stroke->r, stroke->g, stroke->b, stroke->a);
    break;
  default:
    cairo_set_source_rgba(cr, stroke->r, stroke->g, stroke->b, stroke->a);
    break;
  }

  if (stroke->tool == SWASH_TOOL_TEXT) {
    if (stroke->text != NULL && len >= 1) {
      const SwashPoint *point = &g_array_index(stroke->points, SwashPoint, 0);

      cairo_save(cr);
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, stroke->width);
      cairo_move_to(cr, point->x, point->y);
      cairo_show_text(cr, stroke->text);
      cairo_restore(cr);
    }
    return;
  }

  if (stroke->tool == SWASH_TOOL_NUMBERING) {
    if (len >= 1) {
      const SwashPoint *point = &g_array_index(stroke->points, SwashPoint, 0);
      const double circle_radius = stroke->width / 2.0;
      const double font_size = stroke->width * 0.6;
      const char *label = stroke->text != NULL ? stroke->text : "?";
      const double luminance = 0.299 * stroke->r + 0.587 * stroke->g + 0.114 * stroke->b;
      cairo_text_extents_t extents;

      cairo_save(cr);
      cairo_arc(cr, point->x, point->y, circle_radius, 0.0, 2.0 * G_PI);
      cairo_fill(cr);

      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, font_size);
      cairo_text_extents(cr, label, &extents);

      if (luminance > 0.5)
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, stroke->a);
      else
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, stroke->a);

      cairo_move_to(cr,
                    point->x - extents.width / 2.0 - extents.x_bearing,
                    point->y - extents.height / 2.0 - extents.y_bearing);
      cairo_show_text(cr, label);
      cairo_restore(cr);
    }
    return;
  }

  if (stroke->tool == SWASH_TOOL_MARKER) {
    const double marker_step = MIN(2.0, MAX(1.0, stroke->width / 4.0));

    swash_marker_add_rect(cr,
                             g_array_index(stroke->points, SwashPoint, 0).x,
                             g_array_index(stroke->points, SwashPoint, 0).y,
                             stroke->width);

    for (i = 1; i < len; i++) {
      const SwashPoint *previous = &g_array_index(stroke->points, SwashPoint, i - 1);
      const SwashPoint *point = &g_array_index(stroke->points, SwashPoint, i);
      const double dx = point->x - previous->x;
      const double dy = point->y - previous->y;
      const double distance = hypot(dx, dy);
      const int steps = MAX(1, (int) ceil(distance / marker_step));

      for (int step = 1; step <= steps; step++) {
        const double t = (double) step / steps;

        swash_marker_add_rect(cr,
                                 previous->x + dx * t,
                                 previous->y + dy * t,
                                 stroke->width);
      }
    }

    cairo_fill(cr);

    return;
  }

  if (swash_tool_is_shape(stroke->tool) && len >= 2) {
    const SwashPoint *start = &g_array_index(stroke->points, SwashPoint, 0);
    const SwashPoint *end = &g_array_index(stroke->points, SwashPoint, len - 1);
    const double left = MIN(start->x, end->x);
    const double top = MIN(start->y, end->y);
    const double rect_width = fabs(end->x - start->x);
    const double rect_height = fabs(end->y - start->y);

    switch (stroke->tool) {
    case SWASH_TOOL_RECTANGLE:
      cairo_rectangle(cr, left, top, rect_width, rect_height);
      if (stroke->fill_a > 0.0) {
        cairo_set_source_rgba(cr, stroke->fill_r, stroke->fill_g, stroke->fill_b, stroke->fill_a);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, stroke->r, stroke->g, stroke->b, stroke->a);
      }
      cairo_stroke(cr);
      return;
    case SWASH_TOOL_CIRCLE: {
      const double radius_x = rect_width / 2.0;
      const double radius_y = rect_height / 2.0;

      cairo_save(cr);
      cairo_translate(cr, left + radius_x, top + radius_y);
      cairo_scale(cr, MAX(radius_x, 0.0001), MAX(radius_y, 0.0001));
      cairo_arc(cr, 0.0, 0.0, 1.0, 0.0, 2.0 * G_PI);
      cairo_restore(cr);
      if (stroke->fill_a > 0.0) {
        cairo_set_source_rgba(cr, stroke->fill_r, stroke->fill_g, stroke->fill_b, stroke->fill_a);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, stroke->r, stroke->g, stroke->b, stroke->a);
      }
      cairo_stroke(cr);
      return;
    }
    case SWASH_TOOL_LINE:
      cairo_move_to(cr, start->x, start->y);
      cairo_line_to(cr, end->x, end->y);
      cairo_stroke(cr);
      return;
    case SWASH_TOOL_ARROW: {
      const double angle = atan2(end->y - start->y, end->x - start->x);
      const double arrow_size = MAX(12.0, stroke->width * 3.0);

      cairo_move_to(cr, start->x, start->y);
      cairo_line_to(cr, end->x, end->y);
      cairo_stroke(cr);

      cairo_move_to(cr, end->x, end->y);
      cairo_line_to(cr,
                    end->x - arrow_size * cos(angle - G_PI / 6.0),
                    end->y - arrow_size * sin(angle - G_PI / 6.0));
      cairo_move_to(cr, end->x, end->y);
      cairo_line_to(cr,
                    end->x - arrow_size * cos(angle + G_PI / 6.0),
                    end->y - arrow_size * sin(angle + G_PI / 6.0));
      cairo_stroke(cr);
      return;
    }
    case SWASH_TOOL_BLUR: {
      int block_size;
      int b_left;
      int b_top;
      int b_right;
      int b_bottom;
      int tw;
      int th;

      if (source_surface == NULL || cairo_surface_get_type(source_surface) != CAIRO_SURFACE_TYPE_IMAGE)
        return;

      block_size = MAX(2, (int) stroke->width);
      if (stroke->blur_type == 1) {
        b_left = floor(left / block_size) * block_size;
        b_top = floor(top / block_size) * block_size;
        b_right = ceil((left + rect_width) / block_size) * block_size;
        b_bottom = ceil((top + rect_height) / block_size) * block_size;
      } else {
        b_left = floor(left);
        b_top = floor(top);
        b_right = ceil(left + rect_width);
        b_bottom = ceil(top + rect_height);
      }
      tw = b_right - b_left;
      th = b_bottom - b_top;
      if (tw <= 0 || th <= 0)
        return;

      if (stroke->blur_cache == NULL
          || stroke->blur_cache_generation != image_generation
          || stroke->blur_cache_x != b_left
          || stroke->blur_cache_y != b_top
          || cairo_image_surface_get_width(stroke->blur_cache) != tw
          || cairo_image_surface_get_height(stroke->blur_cache) != th) {
        int src_w;
        int src_h;
        int src_stride;
        unsigned char *src_data;
        unsigned char *dst_data;
        int dst_stride;

        cairo_surface_flush(source_surface);
        src_w = cairo_image_surface_get_width(source_surface);
        src_h = cairo_image_surface_get_height(source_surface);
        src_stride = cairo_image_surface_get_stride(source_surface);
        src_data = cairo_image_surface_get_data(source_surface);

        if (src_data == NULL || src_w <= 0 || src_h <= 0)
          return;

        if (stroke->blur_cache != NULL)
          cairo_surface_destroy(stroke->blur_cache);

        stroke->blur_cache = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, tw, th);
        stroke->blur_cache_generation = image_generation;
        stroke->blur_cache_x = b_left;
        stroke->blur_cache_y = b_top;
        dst_data = cairo_image_surface_get_data(stroke->blur_cache);
        dst_stride = cairo_image_surface_get_stride(stroke->blur_cache);

        if (stroke->blur_type == 1) {
          int y;

          for (y = 0; y < th; y += block_size) {
            int x;

            for (x = 0; x < tw; x += block_size) {
              int cx = CLAMP(b_left + x + block_size / 2, 0, src_w - 1);
              int cy = CLAMP(b_top + y + block_size / 2, 0, src_h - 1);
              uint32_t pixel = *(uint32_t *) (src_data + cy * src_stride + cx * 4);
              int by;

              for (by = 0; by < block_size && y + by < th; by++) {
                uint32_t *dst_row = (uint32_t *) (dst_data + (y + by) * dst_stride);
                int bx;

                for (bx = 0; bx < block_size && x + bx < tw; bx++)
                  dst_row[x + bx] = pixel;
              }
            }
          }
        } else {
          swash_blur_region(src_data,
                            src_w,
                            src_h,
                            src_stride,
                            dst_data,
                            dst_stride,
                            b_left,
                            b_top,
                            tw,
                            th,
                            stroke->width);
        }

        cairo_surface_mark_dirty(stroke->blur_cache);
      }

      cairo_save(cr);
      cairo_rectangle(cr, left, top, rect_width, rect_height);
      cairo_clip(cr);
      cairo_set_source_surface(cr, stroke->blur_cache, stroke->blur_cache_x, stroke->blur_cache_y);
      cairo_paint(cr);
      cairo_restore(cr);
      return;
    }
    default:
      break;
    }
  }

  if (len == 1) {
    const SwashPoint *point = &g_array_index(stroke->points, SwashPoint, 0);

    cairo_arc(cr, point->x, point->y, stroke->width / 2.0, 0.0, 2.0 * G_PI);
    cairo_fill(cr);
    return;
  }

  cairo_move_to(cr,
                g_array_index(stroke->points, SwashPoint, 0).x,
                g_array_index(stroke->points, SwashPoint, 0).y);

  for (i = 1; i < len; i++) {
    const SwashPoint *point = &g_array_index(stroke->points, SwashPoint, i);

    cairo_line_to(cr, point->x, point->y);
  }

  cairo_stroke(cr);
}

static double
swash_distance_to_segment(double px,
                             double py,
                             double x0,
                             double y0,
                             double x1,
                             double y1)
{
  const double dx = x1 - x0;
  const double dy = y1 - y0;
  const double length_squared = dx * dx + dy * dy;

  if (length_squared <= 0.0001)
    return hypot(px - x0, py - y0);

  const double t = CLAMP(((px - x0) * dx + (py - y0) * dy) / length_squared, 0.0, 1.0);
  const double closest_x = x0 + t * dx;
  const double closest_y = y0 + t * dy;

  return hypot(px - closest_x, py - closest_y);
}

static gboolean
swash_segment_intersects_rect(double x0,
                                 double y0,
                                 double x1,
                                 double y1,
                                 double left,
                                 double top,
                                 double right,
                                 double bottom)
{
  double t0 = 0.0;
  double t1 = 1.0;
  const double dx = x1 - x0;
  const double dy = y1 - y0;

  if ((x0 >= left && x0 <= right && y0 >= top && y0 <= bottom)
      || (x1 >= left && x1 <= right && y1 >= top && y1 <= bottom))
    return TRUE;

  if (fabs(dx) < 0.0001) {
    if (x0 < left || x0 > right)
      return FALSE;
  } else {
    double tx_min = (left - x0) / dx;
    double tx_max = (right - x0) / dx;

    if (tx_min > tx_max) {
      const double swap = tx_min;

      tx_min = tx_max;
      tx_max = swap;
    }

    t0 = MAX(t0, tx_min);
    t1 = MIN(t1, tx_max);
    if (t0 > t1)
      return FALSE;
  }

  if (fabs(dy) < 0.0001) {
    if (y0 < top || y0 > bottom)
      return FALSE;
  } else {
    double ty_min = (top - y0) / dy;
    double ty_max = (bottom - y0) / dy;

    if (ty_min > ty_max) {
      const double swap = ty_min;

      ty_min = ty_max;
      ty_max = swap;
    }

    t0 = MAX(t0, ty_min);
    t1 = MIN(t1, ty_max);
    if (t0 > t1)
      return FALSE;
  }

  return TRUE;
}

static gboolean
swash_text_intersects_segment(SwashStroke *stroke,
                                 double          x0,
                                 double          y0,
                                 double          x1,
                                 double          y1,
                                 double          radius)
{
  cairo_surface_t *surface;
  cairo_t *cr;
  cairo_text_extents_t extents;
  const SwashPoint *point;
  double left;
  double top;
  double right;
  double bottom;

  if (stroke->points->len == 0 || stroke->text == NULL || stroke->text[0] == '\0')
    return FALSE;

  point = &g_array_index(stroke->points, SwashPoint, 0);
  surface = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
  cr = cairo_create(surface);

  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, stroke->width);
  cairo_text_extents(cr, stroke->text, &extents);

  cairo_destroy(cr);
  cairo_surface_destroy(surface);

  left = point->x + extents.x_bearing - radius;
  top = point->y + extents.y_bearing - radius;
  right = left + extents.width + radius * 2.0;
  bottom = top + extents.height + radius * 2.0;

  return swash_segment_intersects_rect(x0, y0, x1, y1, left, top, right, bottom);
}

gboolean
swash_stroke_intersects_segment(SwashStroke *stroke,
                                   double          x0,
                                   double          y0,
                                   double          x1,
                                   double          y1,
                                   double          radius)
{
  guint i;

  if (stroke->tool == SWASH_TOOL_TEXT)
    return swash_text_intersects_segment(stroke, x0, y0, x1, y1, radius);

  if (stroke->tool == SWASH_TOOL_NUMBERING && stroke->points->len >= 1) {
    const SwashPoint *center = &g_array_index(stroke->points, SwashPoint, 0);
    const double circle_radius = stroke->width / 2.0;

    return swash_distance_to_segment(center->x, center->y, x0, y0, x1, y1) <= radius + circle_radius;
  }

  if (swash_tool_is_shape(stroke->tool) && stroke->points->len >= 2) {
    const SwashPoint *start = &g_array_index(stroke->points, SwashPoint, 0);
    const SwashPoint *end = &g_array_index(stroke->points, SwashPoint, stroke->points->len - 1);
    const double left = MIN(start->x, end->x);
    const double right = MAX(start->x, end->x);
    const double top = MIN(start->y, end->y);
    const double bottom = MAX(start->y, end->y);

    switch (stroke->tool) {
    case SWASH_TOOL_LINE:
    case SWASH_TOOL_ARROW:
      return swash_distance_to_segment(start->x, start->y, x0, y0, x1, y1) <= radius + stroke->width / 2.0
          || swash_distance_to_segment(end->x, end->y, x0, y0, x1, y1) <= radius + stroke->width / 2.0
          || swash_distance_to_segment(x0, y0, start->x, start->y, end->x, end->y) <= radius + stroke->width / 2.0
          || swash_distance_to_segment(x1, y1, start->x, start->y, end->x, end->y) <= radius + stroke->width / 2.0;
    case SWASH_TOOL_BLUR:
      return swash_segment_intersects_rect(x0, y0, x1, y1, left, top, right, bottom);
    case SWASH_TOOL_RECTANGLE:
      return swash_distance_to_segment(x0, y0, left, top, right, top) <= radius + stroke->width / 2.0
          || swash_distance_to_segment(x0, y0, right, top, right, bottom) <= radius + stroke->width / 2.0
          || swash_distance_to_segment(x0, y0, right, bottom, left, bottom) <= radius + stroke->width / 2.0
          || swash_distance_to_segment(x0, y0, left, bottom, left, top) <= radius + stroke->width / 2.0
          || swash_distance_to_segment(x1, y1, left, top, right, top) <= radius + stroke->width / 2.0
          || swash_distance_to_segment(x1, y1, right, top, right, bottom) <= radius + stroke->width / 2.0
          || swash_distance_to_segment(x1, y1, right, bottom, left, bottom) <= radius + stroke->width / 2.0
          || swash_distance_to_segment(x1, y1, left, bottom, left, top) <= radius + stroke->width / 2.0;
    case SWASH_TOOL_CIRCLE: {
      const double center_x = (start->x + end->x) / 2.0;
      const double center_y = (start->y + end->y) / 2.0;
      const double radius_x = MAX(fabs(end->x - start->x) / 2.0, 0.0001);
      const double radius_y = MAX(fabs(end->y - start->y) / 2.0, 0.0001);
      const SwashPoint candidates[] = { { x0, y0 }, { x1, y1 } };

      for (guint j = 0; j < G_N_ELEMENTS(candidates); j++) {
        const double dx = (candidates[j].x - center_x) / radius_x;
        const double dy = (candidates[j].y - center_y) / radius_y;
        const double distance = fabs(hypot(dx, dy) - 1.0) * MIN(radius_x, radius_y);

        if (distance <= radius + stroke->width / 2.0)
          return TRUE;
      }

      return FALSE;
    }
    default:
      break;
    }
  }

  if (stroke->points->len == 0)
    return FALSE;

  for (i = 0; i < stroke->points->len; i++) {
    const SwashPoint *point = &g_array_index(stroke->points, SwashPoint, i);

    if (swash_distance_to_segment(point->x, point->y, x0, y0, x1, y1) <= radius + stroke->width / 2.0)
      return TRUE;
  }

  return FALSE;
}

gboolean
swash_stroke_hit_test(SwashStroke *stroke,
                         double          x,
                         double          y,
                         double          tolerance)
{
  return swash_stroke_intersects_segment(stroke, x, y, x, y, tolerance);
}

void
swash_stroke_offset(SwashStroke *stroke,
                       double          dx,
                       double          dy)
{
  for (guint i = 0; i < stroke->points->len; i++) {
    SwashPoint *point = &g_array_index(stroke->points, SwashPoint, i);

    point->x += dx;
    point->y += dy;
  }

  if (stroke->blur_cache != NULL) {
    cairo_surface_destroy(stroke->blur_cache);
    stroke->blur_cache = NULL;
  }
}

void
swash_stroke_get_bounds(SwashStroke *stroke,
                           double         *out_x,
                           double         *out_y,
                           double         *out_w,
                           double         *out_h)
{
  double min_x = G_MAXDOUBLE;
  double min_y = G_MAXDOUBLE;
  double max_x = -G_MAXDOUBLE;
  double max_y = -G_MAXDOUBLE;
  const double half_w = stroke->width / 2.0;

  if (stroke->points->len == 0) {
    *out_x = *out_y = *out_w = *out_h = 0.0;
    return;
  }

  if (stroke->tool == SWASH_TOOL_NUMBERING && stroke->points->len >= 1) {
    const SwashPoint *p = &g_array_index(stroke->points, SwashPoint, 0);

    *out_x = p->x - half_w;
    *out_y = p->y - half_w;
    *out_w = stroke->width;
    *out_h = stroke->width;
    return;
  }

  if (stroke->tool == SWASH_TOOL_TEXT && stroke->text != NULL && *stroke->text != '\0') {
    const SwashPoint *p = &g_array_index(stroke->points, SwashPoint, 0);
    cairo_surface_t *tmp = cairo_image_surface_create(CAIRO_FORMAT_A1, 1, 1);
    cairo_t *cr = cairo_create(tmp);
    cairo_text_extents_t extents;
    cairo_font_extents_t font_ext;

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, stroke->width);
    cairo_text_extents(cr, stroke->text, &extents);
    cairo_font_extents(cr, &font_ext);
    cairo_destroy(cr);
    cairo_surface_destroy(tmp);

    *out_x = p->x + extents.x_bearing;
    *out_y = p->y - font_ext.ascent;
    *out_w = extents.x_advance - extents.x_bearing;
    *out_h = font_ext.ascent + font_ext.descent;
    return;
  }

  for (guint i = 0; i < stroke->points->len; i++) {
    const SwashPoint *p = &g_array_index(stroke->points, SwashPoint, i);

    if (p->x < min_x) min_x = p->x;
    if (p->y < min_y) min_y = p->y;
    if (p->x > max_x) max_x = p->x;
    if (p->y > max_y) max_y = p->y;
  }

  *out_x = min_x - half_w;
  *out_y = min_y - half_w;
  *out_w = (max_x - min_x) + stroke->width;
  *out_h = (max_y - min_y) + stroke->width;
}

void
swash_strokes_renumber(GPtrArray *strokes)
{
  int count = 0;

  if (strokes == NULL)
    return;

  for (guint i = 0; i < strokes->len; i++) {
    SwashStroke *stroke = g_ptr_array_index(strokes, i);

    if (stroke->tool == SWASH_TOOL_NUMBERING) {
      count++;
      g_free(stroke->text);
      stroke->text = g_strdup_printf("%d", count);
    }
  }
}
