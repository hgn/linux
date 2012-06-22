#ifndef __STUDIO_UTILS_H
#define __STUDIO_UTILS_H


#include <gtk/gtk.h>

gchar *studio_utils_human_time_diff(const gchar *time_str);
unsigned long long extract_digit(gchar *);

/* gtk functions */
GdkPixbuf *load_pixbuf_from_file(const char *);

#endif /* __STUDIO_UTILS_H */
