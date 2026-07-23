#pragma once

#include <adwaita.h>
#include <cairo.h>

typedef enum {
  SWASH_TOOL_PAN,
  SWASH_TOOL_CROP,
  SWASH_TOOL_BRUSH,
  SWASH_TOOL_MARKER,
  SWASH_TOOL_ERASER,
  SWASH_TOOL_RECTANGLE,
  SWASH_TOOL_CIRCLE,
  SWASH_TOOL_LINE,
  SWASH_TOOL_ARROW,
  SWASH_TOOL_OCR,
  SWASH_TOOL_TEXT,
  SWASH_TOOL_BLUR,
  SWASH_TOOL_NUMBERING,
  SWASH_TOOL_MOVE,
} SwashTool;

//this is so annoying
typedef enum {
  SWASH_WINDOW_BACKGROUND_FOLLOW_SYSTEM,
  SWASH_WINDOW_BACKGROUND_OPAQUE,
  SWASH_WINDOW_BACKGROUND_TRANSPARENT,
} SwashWindowBackgroundMode;

typedef struct {
  double x;
  double y;
} SwashPoint;

typedef struct {
  SwashTool tool;
  double width;
  double r;
  double g;
  double b;
  double a;
  double fill_r;
  double fill_g;
  double fill_b;
  double fill_a;
  int blur_type;
  GArray *points;
  char *text;
  cairo_surface_t *blur_cache;
  guint blur_cache_generation;
  int blur_cache_x;
  int blur_cache_y;
} SwashStroke;

typedef struct {
  int left;
  int top;
  int width;
  int height;
  char *text;
  GtkWidget *button;
} SwashOcrLine;

typedef struct {
  guint generation;
  int width;
  int height;
  gsize stride;
  guchar *pixels;
} SwashOcrRequest;

typedef struct {
  guint generation;
  GPtrArray *lines;
} SwashOcrResult;

typedef enum {
  SWASH_EXPORT_COPY,
  SWASH_EXPORT_SAVE,
} SwashExportKind;

typedef SwashStroke *(*SwashStrokeCopyFunc)(SwashStroke *stroke);
typedef void (*SwashStrokeRenderFunc)(cairo_t *cr,
                                         SwashStroke *stroke,
                                         cairo_surface_t *source_surface,
                                         guint image_generation);

typedef struct {
  SwashExportKind kind;
  int width;
  int height;
  gsize stride;
  guchar *pixels;
  GPtrArray *strokes;
  GFile *file;
  char *copy_format;
  gboolean allow_marker_overlap;
  SwashStrokeRenderFunc render_stroke;
  guint image_generation;
} SwashExportRequest;

typedef struct {
  const char *mime_type;
  GBytes *bytes;
  GdkTexture *texture;
} SwashCopyResult;
