#ifndef __STUDIO_DB_H
#define __STUDIO_DB_H

#define DB_PROJECT_NAME_MAX 512

#include "builtin-studio.h"

#include <gtk/gtk.h>

struct db_project_summary {
	gchar *name;
	gchar *path;
	gchar *last_accessed;
};


struct db_projects_summary {
	GSList *list;
};

struct perf_project {
	gchar *name;
	gchar *executable_path;
	gchar *working_dir;

	struct studio_context *sc;
};

struct perf_project_run {
	GList *event_list;
	GList *run_complete_finish_list;
	struct perf_project *perf_project;
};


int db_global_init(void);

/* forward declaration */
struct studio_assitant_new_project_data;

gboolean db_generic_get_projects_summaries(struct db_projects_summary **xps);
void db_generic_get_projects_summary_free(struct db_projects_summary *ps);
const char *db_get_last_project_path(void);
void db_local_generate_project_file(struct studio_assitant_new_project_data *pd);
bool db_local_get_perf_project(struct studio_context *sc, gchar *project_path, struct perf_project **pd);
void perf_project_free(struct perf_project *);


#endif	/* __STUDIO_DB_H */
