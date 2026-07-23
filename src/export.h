#pragma once

#include "types.h"

SwashExportRequest *swash_export_request_new(GdkTexture            *texture,
                                                   GPtrArray             *strokes,
                                                   SwashExportKind     kind,
                                                   GFile                 *file,
                                                   const char            *copy_format,
                                                   SwashStrokeCopyFunc copy_stroke,
                                                   GDestroyNotify         stroke_free,
                                                   gboolean               allow_marker_overlap,
                                                   SwashStrokeRenderFunc render_stroke,
                                                   guint                  image_generation,
                                                   GError               **error);
void swash_export_request_free(SwashExportRequest *request);

void swash_copy_result_free(SwashCopyResult *result);
void swash_export_run_task(GTask        *task,
                              gpointer      source_object,
                              gpointer      task_data,
                              GCancellable *cancellable);
