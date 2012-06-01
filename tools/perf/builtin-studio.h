#ifndef __BUILTIN_STUDIO_H
#define __BUILTIN_STUDIO_H

#include <linux/types.h>
#include <pthread.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "util/evlist.h"


struct studio_context {
	char *homedirpath;
	struct {
		GtkWidget *main_window;
		GtkItemFactory *main_menu;
		GtkWidget *vbox;

		GtkWidget *sw;

		GtkWidget *main_paned;
		GtkWidget *main_paned_control;
		GtkWidget *main_paned_workspace;

		GtkAccelGroup *accel_group;

		GtkWidget *statusbar;

		GtkWidget *dialog_window;

		/* notebook data */
		GtkWidget *schart;

		/* Modal window displayed if command
		 * is executed and sampling/counting
		 * is active
		 */
		GtkWidget *perf_run_window;

		int theme;
	} screen;

	struct {
		gchar *global_conf_path;
	} db;

	/* location to pixmaps and artwork */
	char *pixmapdir;
	char *buttondir;
};

int perf_evlist__studio_browse_hists(struct perf_evlist *evlist, const char *help,
		void(*timer)(void *arg), void *arg,
		int refresh);

#endif	/* __BUILTIN_STUDIO_H*/

