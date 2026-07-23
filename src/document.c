#include "document.h"

#include "stroke.h"

#define SWASH_HISTORY_LIMIT 50

typedef struct {
  GBytes *image_pixels;
  int image_width;
  int image_height;
  gsize image_stride;
  GPtrArray *strokes;
} SwashDocumentSnapshot;

struct _SwashDocument {
  GPtrArray *strokes;
  GBytes *image_pixels;
  int image_width;
  int image_height;
  gsize image_stride;
  guint image_generation;
  SwashDocumentSnapshot *saved_state;
  GQueue *undo_history;
  GQueue *redo_history;
};

static GPtrArray *
swash_stroke_array_copy(GPtrArray *strokes)
{
  GPtrArray *copy = g_ptr_array_new_with_free_func((GDestroyNotify) swash_stroke_free);

  if (strokes == NULL)
    return copy;

  for (guint i = 0; i < strokes->len; i++)
    g_ptr_array_add(copy, swash_stroke_copy(g_ptr_array_index(strokes, i)));

  return copy;
}

static SwashDocumentSnapshot *
swash_document_snapshot_new(GBytes    *image_pixels,
                               int        image_width,
                               int        image_height,
                               gsize      image_stride,
                               GPtrArray *strokes)
{
  SwashDocumentSnapshot *snapshot = g_new0(SwashDocumentSnapshot, 1);

  snapshot->image_pixels = image_pixels != NULL ? g_bytes_ref(image_pixels) : NULL;
  snapshot->image_width = image_width;
  snapshot->image_height = image_height;
  snapshot->image_stride = image_stride;
  snapshot->strokes = swash_stroke_array_copy(strokes);
  return snapshot;
}

static SwashDocumentSnapshot *
swash_document_snapshot_from_document(SwashDocument *document)
{
  return swash_document_snapshot_new(document->image_pixels,
                                        document->image_width,
                                        document->image_height,
                                        document->image_stride,
                                        document->strokes);
}

static void
swash_document_snapshot_free(SwashDocumentSnapshot *snapshot)
{
  if (snapshot == NULL)
    return;

  g_clear_pointer(&snapshot->image_pixels, g_bytes_unref);
  g_clear_pointer(&snapshot->strokes, g_ptr_array_unref);
  g_free(snapshot);
}

static gboolean
swash_stroke_equal(SwashStroke *left,
                      SwashStroke *right)
{
  if (left == right)
    return TRUE;

  if (left == NULL || right == NULL)
    return FALSE;

  if (left->tool != right->tool
      || left->width != right->width
      || left->r != right->r
      || left->g != right->g
      || left->b != right->b
      || left->a != right->a
      || left->blur_type != right->blur_type
      || left->points->len != right->points->len
      || g_strcmp0(left->text, right->text) != 0)
    return FALSE;

  return memcmp(left->points->data,
                right->points->data,
                left->points->len * sizeof(SwashPoint)) == 0;
}

static gboolean
swash_stroke_array_equal(GPtrArray *left,
                            GPtrArray *right)
{
  if (left == right)
    return TRUE;

  if (left == NULL || right == NULL || left->len != right->len)
    return FALSE;

  for (guint i = 0; i < left->len; i++) {
    if (!swash_stroke_equal(g_ptr_array_index(left, i),
                               g_ptr_array_index(right, i)))
      return FALSE;
  }

  return TRUE;
}

static gboolean
swash_document_image_equal(GBytes *left_pixels,
                              int     left_width,
                              int     left_height,
                              gsize   left_stride,
                              GBytes *right_pixels,
                              int     right_width,
                              int     right_height,
                              gsize   right_stride)
{
  if (left_pixels == right_pixels)
    return left_width == right_width
        && left_height == right_height
        && left_stride == right_stride;

  if (left_pixels == NULL || right_pixels == NULL)
    return FALSE;

  return left_width == right_width
      && left_height == right_height
      && left_stride == right_stride
      && g_bytes_equal(left_pixels, right_pixels);
}

static gboolean
swash_document_snapshot_equal(SwashDocumentSnapshot *left,
                                 SwashDocumentSnapshot *right)
{
  if (left == right)
    return TRUE;

  if (left == NULL || right == NULL)
    return FALSE;

  return swash_document_image_equal(left->image_pixels,
                                       left->image_width,
                                       left->image_height,
                                       left->image_stride,
                                       right->image_pixels,
                                       right->image_width,
                                       right->image_height,
                                       right->image_stride)
      && swash_stroke_array_equal(left->strokes, right->strokes);
}

static void
swash_document_apply_snapshot(SwashDocument         *document,
                                 SwashDocumentSnapshot *snapshot)
{
  gboolean image_changed = document->image_pixels != snapshot->image_pixels;

  g_clear_pointer(&document->image_pixels, g_bytes_unref);
  g_clear_pointer(&document->strokes, g_ptr_array_unref);

  document->image_pixels = g_steal_pointer(&snapshot->image_pixels);
  document->image_width = snapshot->image_width;
  document->image_height = snapshot->image_height;
  document->image_stride = snapshot->image_stride;
  document->strokes = g_steal_pointer(&snapshot->strokes);

  if (image_changed)
    document->image_generation++;

  swash_document_snapshot_free(snapshot);
}

static void
swash_document_trim_history(GQueue *history)
{
  while (g_queue_get_length(history) > SWASH_HISTORY_LIMIT) {
    SwashDocumentSnapshot *snapshot = g_queue_pop_head(history);

    swash_document_snapshot_free(snapshot);
  }
}

SwashDocument *
swash_document_new(void)
{
  SwashDocument *document = g_new0(SwashDocument, 1);

  document->strokes = g_ptr_array_new_with_free_func((GDestroyNotify) swash_stroke_free);
  document->undo_history = g_queue_new();
  document->redo_history = g_queue_new();
  return document;
}

void
swash_document_free(SwashDocument *document)
{
  if (document == NULL)
    return;

  swash_document_clear_history(document);
  g_clear_pointer(&document->image_pixels, g_bytes_unref);
  g_clear_pointer(&document->strokes, g_ptr_array_unref);
  swash_document_snapshot_free(document->saved_state);
  g_clear_pointer(&document->undo_history, g_queue_free);
  g_clear_pointer(&document->redo_history, g_queue_free);
  g_free(document);
}

GPtrArray *
swash_document_get_strokes(SwashDocument *document)
{
  return document->strokes;
}

gboolean
swash_document_get_image(SwashDocument *document,
                            GBytes          **pixels,
                            int              *width,
                            int              *height,
                            gsize            *stride)
{
  if (document->image_pixels == NULL)
    return FALSE;

  if (pixels != NULL)
    *pixels = g_bytes_ref(document->image_pixels);
  if (width != NULL)
    *width = document->image_width;
  if (height != NULL)
    *height = document->image_height;
  if (stride != NULL)
    *stride = document->image_stride;

  return TRUE;
}

void
swash_document_set_image(SwashDocument *document,
                            GBytes           *pixels,
                            int               width,
                            int               height,
                            gsize             stride)
{
  g_clear_pointer(&document->image_pixels, g_bytes_unref);
  document->image_pixels = pixels != NULL ? g_bytes_ref(pixels) : NULL;
  document->image_width = pixels != NULL ? width : 0;
  document->image_height = pixels != NULL ? height : 0;
  document->image_stride = pixels != NULL ? stride : 0;
  document->image_generation++;
}

guint
swash_document_get_image_generation(SwashDocument *document)
{
  return document->image_generation;
}

gboolean
swash_document_has_unsaved_changes(SwashDocument *document)
{
  SwashDocumentSnapshot *current_state = swash_document_snapshot_from_document(document);
  gboolean has_unsaved_changes = !swash_document_snapshot_equal(current_state, document->saved_state);

  swash_document_snapshot_free(current_state);
  return has_unsaved_changes;
}

void
swash_document_mark_saved(SwashDocument *document)
{
  swash_document_snapshot_free(document->saved_state);
  document->saved_state = swash_document_snapshot_from_document(document);
}

gboolean
swash_document_can_undo(SwashDocument *document)
{
  return !g_queue_is_empty(document->undo_history);
}

gboolean
swash_document_can_redo(SwashDocument *document)
{
  return !g_queue_is_empty(document->redo_history);
}

void
swash_document_record_undo_step(SwashDocument *document)
{
  g_queue_push_tail(document->undo_history, swash_document_snapshot_from_document(document));
  swash_document_trim_history(document->undo_history);
  g_queue_clear_full(document->redo_history, (GDestroyNotify) swash_document_snapshot_free);
}

GPtrArray *
swash_document_discard_undo_step(SwashDocument *document)
{
  SwashDocumentSnapshot *snapshot;

  if (g_queue_is_empty(document->undo_history))
    return document->strokes;

  snapshot = g_queue_pop_tail(document->undo_history);
  swash_document_apply_snapshot(document, snapshot);
  return document->strokes;
}

GPtrArray *
swash_document_undo(SwashDocument *document)
{
  SwashDocumentSnapshot *snapshot;

  if (g_queue_is_empty(document->undo_history))
    return NULL;

  g_queue_push_tail(document->redo_history, swash_document_snapshot_from_document(document));
  swash_document_trim_history(document->redo_history);
  snapshot = g_queue_pop_tail(document->undo_history);
  swash_document_apply_snapshot(document, snapshot);
  return document->strokes;
}

GPtrArray *
swash_document_redo(SwashDocument *document)
{
  SwashDocumentSnapshot *snapshot;

  if (g_queue_is_empty(document->redo_history))
    return NULL;

  g_queue_push_tail(document->undo_history, swash_document_snapshot_from_document(document));
  swash_document_trim_history(document->undo_history);
  snapshot = g_queue_pop_tail(document->redo_history);
  swash_document_apply_snapshot(document, snapshot);
  return document->strokes;
}

void
swash_document_clear_history(SwashDocument *document)
{
  if (document->undo_history != NULL)
    g_queue_clear_full(document->undo_history, (GDestroyNotify) swash_document_snapshot_free);

  if (document->redo_history != NULL)
    g_queue_clear_full(document->redo_history, (GDestroyNotify) swash_document_snapshot_free);
}

void
swash_document_set_strokes(SwashDocument *document,
                              GPtrArray        *strokes)
{
  g_clear_pointer(&document->strokes, g_ptr_array_unref);
  document->strokes = strokes;
}

void
swash_document_clear_annotations(SwashDocument *document)
{
  if (document->strokes != NULL)
    g_ptr_array_set_size(document->strokes, 0);
}
