#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define SWASH_TYPE_WINDOW (swash_window_get_type())

G_DECLARE_FINAL_TYPE(SwashWindow, swash_window, SWASH, WINDOW, AdwApplicationWindow)

SwashWindow *swash_window_new(AdwApplication *app);
gboolean swash_window_open_file(SwashWindow *self,
                                   GFile          *file,
                                   GError        **error);
gboolean swash_window_open_bytes(SwashWindow *self,
                                    GBytes         *bytes,
                                    const char     *display_name,
                                    GError        **error);

void swash_window_save_state(SwashWindow *self);

G_END_DECLS
