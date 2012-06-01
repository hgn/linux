#ifndef __STUDIO_DB_H
#define __STUDIO_DB_H

#define DB_PROJECT_NAME_MAX 512

#include <gtk/gtk.h>

struct db_project_summary {
	gchar *name;
	gchar *path;
	gchar *last_accessed;
};


struct db_projects_summary {
	GSList *list;
};

int db_global_init(void);

gboolean db_generic_get_projects_summaries(struct db_projects_summary **xps);
void db_generic_get_projects_summary_free(struct db_projects_summary *ps);
const char *db_get_last_project_path(void);
bool db_global_generate_project_file(const char *);


#endif	/* __STUDIO_DB_H */
