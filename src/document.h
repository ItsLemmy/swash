#pragma once

#include "types.h"

typedef struct _SwashDocument SwashDocument;

SwashDocument *swash_document_new(void);
void swash_document_free(SwashDocument *document);

GPtrArray *swash_document_get_strokes(SwashDocument *document);
gboolean swash_document_get_image(SwashDocument *document,
                                     GBytes          **pixels,
                                     int              *width,
                                     int              *height,
                                     gsize            *stride);
void swash_document_set_image(SwashDocument *document,
                                 GBytes           *pixels,
                                 int               width,
                                 int               height,
                                 gsize             stride);
gboolean swash_document_has_unsaved_changes(SwashDocument *document);
void swash_document_mark_saved(SwashDocument *document);

gboolean swash_document_can_undo(SwashDocument *document);
gboolean swash_document_can_redo(SwashDocument *document);
void swash_document_record_undo_step(SwashDocument *document);
GPtrArray *swash_document_discard_undo_step(SwashDocument *document);
GPtrArray *swash_document_undo(SwashDocument *document);
GPtrArray *swash_document_redo(SwashDocument *document);
void swash_document_clear_history(SwashDocument *document);

void swash_document_set_strokes(SwashDocument *document,
                                   GPtrArray        *strokes);
void swash_document_clear_annotations(SwashDocument *document);
guint swash_document_get_image_generation(SwashDocument *document);
