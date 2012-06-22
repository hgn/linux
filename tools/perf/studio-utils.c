/*
 * studio-utils.c
 *
 * Written by Hagen Paul Pfeifer <hagen.pfeifer@protocollabs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <assert.h>

#include "studio-utils.h"

gchar *studio_utils_human_time_diff(const gchar *time_str)
{
	time_t t, t_diff;
	unsigned long time_digit;
        char *endptr;

	t = time(NULL);
	if (t == (time_t) -1) {
		return NULL;
	}

	errno = 0;    /* To distinguish success/failure after call */
	time_digit = strtoul(time_str, &endptr, 10);

	/* Check for various possible errors */

	if ((errno == ERANGE && (time_digit == ULONG_MAX))
			|| (errno != 0 && time_digit == 0)) {
		return NULL;
	}

	if (endptr == time_str) {
		return NULL;
	}

	t_diff = t - time_digit;
	if (t_diff < 60)
		return g_strdup_printf("%ld seconds ago", t_diff);
	if (t_diff < 60 * 60)
		return g_strdup_printf("%ld minutes ago", lround((floor((double)t_diff / 60))));

	if (t_diff < 60 * 60 * 24)
		return g_strdup_printf("%ld hours ago", lround(floor((double)t_diff / (60 * 60))));

	return g_strdup_printf("%ld days ago", lround(floor((double)t_diff / (60 * 60 * 24))));
}


GdkPixbuf *load_pixbuf_from_file(const char *filename)
{
    GError *gerror = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(filename, &gerror);

    if (pixbuf == NULL) {
        g_print("Error loading file: %d : %s\n", gerror->code, gerror->message);
        g_error_free(gerror);
        exit(1);
    }

    return pixbuf;
}


unsigned long long extract_digit(gchar *str)
{
        gchar digitized[sizeof(stringify(ULLONG_MAX)) + 1];
        int sindex = 0;
        unsigned long long result;
        int len;

        assert(str);

        len = strlen(str);

        assert(len <= sizeof(stringify(ULLONG_MAX)));
        assert(len > 0);

        for (int i = 0; i < len; i++) {
                if (g_ascii_isdigit(str[i]))
                        digitized[sindex++] = str[i];
                        // memcpy(&sanitized_digit[sindex++], &str[i], 1);
        }

        digitized[sindex] = '\0';
        result = atoi(digitized);

        return result;
}
