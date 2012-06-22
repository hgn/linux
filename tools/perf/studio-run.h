#ifndef __STUDIO_RUN_H
#define __STUDIO_RUN_H


#include <gtk/gtk.h>

#include "builtin-studio.h"

struct studio_common_analyze_data {
	GHashTable *data_table;
};


gboolean studio_run(gpointer);

#endif /* __STUDIO_RUN_H */
