/*
 * studio-run.c
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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>

#include "builtin-studio.h"
#include "studio-run.h"
#include "studio-db.h"

#define COMMON_ANALYSE_FILENAME "perf-common.data"
#define COMMON_ANALYSE_FIELD_SEPARATOR ";"


/* this function can be called if we know
 * that analysis ended. We open the analyze
 * file, parse the content and inform (this
 * time also asynchronously all registered
 * handler and pass over the structurized data
 */
static gboolean studio_run_completed(gpointer priv_data)
{
	GList *list_tmp;
	struct studio_context *sc;
	char *line = NULL;
	size_t len = 0;
	ssize_t nread;
	FILE *fp;
	char key[128];
	unsigned long long value;
	int n;
	struct studio_common_analyze_data studio_common_analyze_data;

	sc = priv_data;

	studio_common_analyze_data.data_table = g_hash_table_new(g_str_hash, g_str_equal);

	fp = fopen(COMMON_ANALYSE_FILENAME, "r");
	if (fp == NULL) {
		pr_err("Cannot open file: %s\n", COMMON_ANALYSE_FILENAME);
		return true;
	}

	while ((nread = getline(&line, &len, fp)) != -1) {
		printf("%s", line);
		n = sscanf(line, "%llu;%s", &value, key);
		if (n == 2) {
			unsigned long long *save_value;
			save_value = g_malloc(sizeof(*save_value));
			*save_value = value;
			fprintf(stderr, "key: %s - value: %llu\n", key, value);
			g_hash_table_insert(studio_common_analyze_data.data_table,
					    g_strdup(key), save_value);
		}
	}

	free(line);


	list_tmp = sc->common_data_changed_cb_list;
	while (list_tmp != NULL) {
		void (*cb)(struct studio_common_analyze_data *);
		cb = list_tmp->data;
		cb(&studio_common_analyze_data);
		list_tmp = g_list_next(list_tmp);
	}

	return true;
}


void studio_run_register_common_data_cb(struct studio_context *sc,
		void (*cb)(struct perf_project_data *, struct studio_common_analyze_data *))
{
	sc->common_data_changed_cb_list = g_list_append(sc->common_data_changed_cb_list, cb);

}


gboolean studio_run(gpointer data)
{
	struct studio_context *sc;
	struct perf_project *pp;
	GError *gerror = NULL;

	assert(data);
	sc = data;
	pp = sc->perf_project_data;

	int exit_status = 0;
	gchar *argv[] = { (char *) "./perf",
		          (char *) "./perf",
		          (char *) "stat",
		          (char *) "-e",
		          (char *) "task-clock,cycles:u,instructions:u",
		          (char *) "-x",
		          (char *) COMMON_ANALYSE_FIELD_SEPARATOR,
		          (char *) "-o",
		          (char *) COMMON_ANALYSE_FILENAME,
		          (char *) "/usr/bin/du",
			  NULL };

	if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_FILE_AND_ARGV_ZERO, NULL, NULL,
		          NULL, NULL, &exit_status, &gerror)) {
		pr_err("exec error: %s\n", gerror->message);
	}

	if (pp->sc->perf_project_data) {
		g_free(pp->sc->perf_project_data);
		pp->sc->perf_project_data = NULL;
	}

	studio_run_completed(pp->sc);

	return TRUE;
}

#define EVENT_NAME_MAX 64

struct event_record {
	int mode;
	char event[EVENT_NAME_MAX];
};


/* data required to start a "perf record | stat". The path to the
 * executable, the workding directory et cetera are available in
 * sc->perf_project_data. */
struct rr_record {
	GList event_record_list;
	void (*report_cb)(struct studio_context *, void *priv_data);
	void *priv_data;

	/* private data */
	struct studio_context *sc;
};

#define EXECUTE_MODE_ASYNC 0x1


/* this function is called asynchronously. We spawn performance
   subsystem and exectute the programm. Ater that we call the registered
   callback functions. This may block the programm for a longer time
   complelty. Plan is record_report_async. Spawns a seperate thread which
   to the processing. To keep things simple and easy extendable we make
   implement the most */
static boolean record_report_async(studio_context *sc, struct rr_record *ed)
{

}

/* increase complexity to decrease complexity */
struct record_report_exchange {
        void (*cb)(struct studio_context *, void *priv_data);
        void *priv_data;
        struct studio_context *sc;
};


/**
 * record_report - execute programm
 *
 * @rr_record : defines what is to do
 * @sc : points to the actual project (path and working dir)
 */
static bool record_report(struct rr_record *ed)
{
	struct studio_context *sc;
        struct record_report_exchange *record_report_exchange;

	assert(ed);

	sc = ed->sc;

        if (sc->mode == STUDIO_MODE_EXECUTION) {
                /* we are still in execution mode
                 * During this time no task can be
                 * registered */
                return false;
        }

        record_report_exchange = g_malloc0(sizeof(*record_report_exchange));
        record_report_exchange->sc = sc;
        record_report_exchange->cb = rr_record->report_cb;
        record_report_exchange->priv_data = rr_record->priv_data;

        /* register this task at call idle with the function
         * to execute it when ready */
        sc->mode = STUDIO_MODE_EXECUTION;

        record_report_async(sc, ed);
        g_idle_add(GSourceFunc function, gpointer data);

        return true;
}


/* example mod */

/* this method is called if the record and module report phase
 * was successfull */
void record_report_cb(struct studio_context *sc, struct rr_record_data *rp)
{

}
