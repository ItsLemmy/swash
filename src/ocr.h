#pragma once

#include "types.h"

SwashOcrLine *swash_ocr_line_new(int         left,
                                       int         top,
                                       int         width,
                                       int         height,
                                       const char *text);
void swash_ocr_line_free(SwashOcrLine *line);

SwashOcrRequest *swash_ocr_request_new(GdkTexture *texture,
                                             guint       generation);
void swash_ocr_request_free(SwashOcrRequest *request);

void swash_ocr_result_free(SwashOcrResult *result);
void swash_ocr_run_task(GTask        *task,
                           gpointer      source_object,
                           gpointer      task_data,
                           GCancellable *cancellable);
