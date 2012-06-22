/*
 * studio-control-overview.c
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

#include "builtin-studio.h"
#include "studio-control-pane.h"
#include "studio-db.h"
#include "studio-run.h"



static GtkWidget *control_overview_empty_new(struct studio_context *sc, gchar *text)
{
	GtkWidget *vbox;
	GtkWidget *widget;
	GtkWidget *scroll_widget;
	GtkWidget *main_frame;

	scroll_widget = gtk_scrolled_window_new(NULL, NULL);

	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_widget),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll_widget),
					    GTK_SHADOW_OUT);

	main_frame = gtk_frame_new(text);
	vbox = gtk_vbox_new(FALSE, 10);


	gtk_container_add(GTK_CONTAINER(main_frame), vbox);
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scroll_widget), main_frame);

	return scroll_widget;
}

#define SCRATCH_BUF_MAX 128

static GtkWidget *screen_mainwindow_control_overview_program_anatomy_new(
		struct studio_context *sc, struct perf_project *pd)
{
	GtkWidget *expander;
	GtkWidget *entry;
	GtkWidget *table;
	GtkTooltips *tooltip;
	struct stat sb;
	char scratch_buf[SCRATCH_BUF_MAX];


	if (stat(pd->executable_path, &sb) == -1)
		return gtk_frame_new("Executable out of date");

	expander= gtk_expander_new("Program Anatomy");

	table = gtk_table_new(8, 2, TRUE);

	gtk_table_set_row_spacings(GTK_TABLE(table), 2);
	gtk_table_set_col_spacings(GTK_TABLE(table), 2);

	entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entry), "Executable");
	gtk_table_attach_defaults(GTK_TABLE(table), entry, 0, 1, 0, 1);

	entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entry), pd->executable_path);
	gtk_table_attach_defaults(GTK_TABLE(table), entry, 1, 2, 0, 1);


	entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entry), "Working Directory");
	gtk_table_attach_defaults(GTK_TABLE(table), entry, 0, 1, 1, 2);

	entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entry), pd->working_dir);
	gtk_table_attach_defaults(GTK_TABLE(table), entry, 1, 2, 1, 2);


	entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entry), "Executable Size:");
	gtk_table_attach_defaults(GTK_TABLE(table), entry, 0, 1, 2, 3);

	entry = gtk_entry_new();
	g_snprintf(scratch_buf, SCRATCH_BUF_MAX - 1, "%lld bytes", (long long) sb.st_size);
	gtk_entry_set_text(GTK_ENTRY(entry), scratch_buf);
	tooltip = gtk_tooltips_new();
	gtk_tooltips_set_tip(tooltip, entry, "Size of the executable in byte", NULL);
	gtk_table_attach_defaults(GTK_TABLE(table), entry, 1, 2, 2, 3);


	entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entry), "Last Modified");
	gtk_table_attach_defaults(GTK_TABLE(table), entry, 0, 1, 3, 4);

	entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entry), ctime(&sb.st_mtime));
	gtk_table_attach_defaults(GTK_TABLE(table), entry, 1, 2, 3, 4);


	entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entry), "Owner:Group");
	gtk_table_attach_defaults(GTK_TABLE(table), entry, 0, 1, 4, 5);

	entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entry), "pfeifer:pfeifer");
	gtk_table_attach_defaults(GTK_TABLE(table), entry, 1, 2, 4, 5);


	entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entry), "BuildID [sha1]");
	gtk_table_attach_defaults(GTK_TABLE(table), entry, 0, 1, 5, 6);

	entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entry), "0x5da35aaed2c163e1d83ae5273d8c9c21967ba49a");
	gtk_table_attach_defaults(GTK_TABLE(table), entry, 1, 2, 5, 6);


	entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entry), "Scheduling Policy");
	gtk_table_attach_defaults(GTK_TABLE(table), entry, 0, 1, 6, 7);

	entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entry), "SCHED_OTHER");
	gtk_table_attach_defaults(GTK_TABLE(table), entry, 1, 2, 6, 7);


	entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entry), "Scheduling Priority");
	gtk_table_attach_defaults(GTK_TABLE(table), entry, 0, 1, 7, 8);

	entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entry), "10");
	gtk_table_attach_defaults(GTK_TABLE(table), entry, 1, 2, 7, 8);


	gtk_container_add(GTK_CONTAINER(expander), table);
	gtk_expander_set_expanded(GTK_EXPANDER(expander), TRUE);

	return expander;
}

#define	COLORF(x) ((float)x/255)

static gboolean screen_mainwindow_control_overview_thread_behavior_draw(GtkWidget *widget,
		GdkEventExpose *event, gpointer data)
{
	cairo_t *cr;
	int i;

	(void) event;
	(void) data;

	/* get a cairo_t */
	cr = gdk_cairo_create(widget->window);

	gtk_widget_set_size_request(widget, 600, 150);

	cairo_rectangle(cr, 0, 0, 600, 150);
	cairo_clip (cr);

	cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);

	cairo_set_line_width(cr, 1);

	cairo_set_source_rgb(cr, 0.25, 0.25, 0.25);
	cairo_rectangle(cr, 5, 5, 600 - 10, 140);
	cairo_fill(cr);
	cairo_set_source_rgb(cr, 0.15, 0.15, 0.15);
	cairo_rectangle(cr, 5, 5, 600 - 10, 140);
	cairo_stroke(cr);


	/* draw grid */
	cairo_set_source_rgb(cr, 0.15, 0.15, 0.15);
	cairo_set_line_width(cr, 1);
	for (i = 5; i < 600; i += 50) {
		cairo_move_to(cr, i, 5);
		cairo_line_to(cr, i, 145);
		cairo_stroke(cr);
	}


	cairo_set_source_rgb(cr, COLORF(102), COLORF(153), COLORF(204));

	cairo_rectangle(cr, 40, 30, 540, 10);
	cairo_fill(cr);

	cairo_rectangle(cr, 50, 50, 240, 10);
	cairo_fill(cr);

	cairo_rectangle(cr, 30, 70, 40, 10);
	cairo_fill(cr);

	cairo_rectangle(cr, 10, 90, 100, 10);
	cairo_fill(cr);

	cairo_rectangle(cr, 80, 110, 440, 10);
	cairo_fill(cr);

	cairo_rectangle(cr, 20, 130, 500, 10);
	cairo_fill(cr);

	cairo_destroy (cr);

	//fprintf(stderr, "- %d - %d\n", g->width , g->height);

	return TRUE;
}



static GtkWidget *screen_mainwindow_control_overview_thread_behavior_new(void)
{
	GtkWidget *expander;
	GtkWidget *darea;

	expander= gtk_expander_new("Thread Runtime");

	darea = gtk_drawing_area_new();

	g_signal_connect(darea, "expose-event", G_CALLBACK(screen_mainwindow_control_overview_thread_behavior_draw), NULL);

	gtk_container_add(GTK_CONTAINER(expander), darea);
	gtk_expander_set_expanded(GTK_EXPANDER (expander), FALSE);

	return expander;
}


static GtkWidget *screen_mainwindow_control_overview_wait_and_block_new(void)
{
	GtkWidget *expander;
	GtkWidget *entry;

	expander= gtk_expander_new("Wait and Blocking Behavior");

	entry = gtk_entry_new();
	gtk_container_add(GTK_CONTAINER(expander), entry);
	gtk_expander_set_expanded(GTK_EXPANDER (expander), FALSE);

	return expander;
}


static GtkWidget *screen_mainwindow_control_overview_segment_sizes_new(void)
{
	GtkWidget *expander;
	GtkWidget *entry;

	expander= gtk_expander_new("Segment Sized");

	entry = gtk_entry_new();
	gtk_container_add(GTK_CONTAINER(expander), entry);
	gtk_expander_set_expanded(GTK_EXPANDER (expander), FALSE);

	return expander;
}


static GtkWidget *screen_mainwindow_control_overview_ks_us_times_new(void)
{
	GtkWidget *expander;
	GtkWidget *entry;

	expander= gtk_expander_new("Kernel Userspace Ratio");

	entry = gtk_entry_new();
	gtk_container_add(GTK_CONTAINER(expander), entry);
	gtk_expander_set_expanded(GTK_EXPANDER (expander), FALSE);

	return expander;
}


static GtkWidget *screen_mainwindow_control_overview_function_sizes_new(void)
{
	GtkWidget *expander;
	GtkWidget *entry;

	expander= gtk_expander_new("Functions Sized");

	entry = gtk_entry_new();
	gtk_container_add(GTK_CONTAINER(expander), entry);
	gtk_expander_set_expanded(GTK_EXPANDER (expander), FALSE);

	return expander;
}

static GtkWidget *screen_mainwindow_control_overview_systemcall_distribution_new(void)
{
	GtkWidget *expander;
	GtkWidget *entry;

	expander= gtk_expander_new("Systemcall Distribution");

	entry = gtk_entry_new();
	gtk_container_add(GTK_CONTAINER(expander), entry);
	gtk_expander_set_expanded(GTK_EXPANDER (expander), FALSE);

	return expander;
}


static GtkWidget *screen_mainwindow_control_overview_rerun(struct studio_context *sc,
		struct perf_project *pd)
{
	GtkWidget *hbox;
	GtkWidget *button;
	GtkWidget *image;
	char image_path[PATH_MAX];

	hbox = gtk_hbox_new(FALSE, 0);

	button = gtk_button_new();
	/* also nice gnome-cpu-frequency-applet.png */
	snprintf(image_path, sizeof(image_path), "%s%s", sc->buttondir, "elisa.png");
	fprintf(stderr, "load image: %s\n", image_path);
	image = gtk_image_new_from_file(image_path);
	gtk_button_set_image(GTK_BUTTON(button), image);
	gtk_button_set_label(GTK_BUTTON(button), "  Update ");
	gtk_widget_set_name(button, "myapp-special-widget");

	g_signal_connect_swapped(G_OBJECT(button), "clicked", G_CALLBACK(studio_run), pd);

	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 5);

	return  hbox;
}


static GtkWidget *control_overview_path_new(struct studio_context *sc, gchar *path)
{
	bool ret;
	GtkWidget *vbox;
	GtkWidget *widget;
	GtkWidget *scroll_widget;
	GtkWidget *main_frame;
	struct perf_project *pd;

	ret = db_local_get_perf_project(sc, path, &pd);
	if (!ret) {
		pr_err("Cannot get project data from %s\n", path);
		return control_overview_empty_new(sc, "Project File Corrupt");
	}

	sc->perf_project_data = pd;


	scroll_widget = gtk_scrolled_window_new(NULL, NULL);

	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_widget),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll_widget),
					    GTK_SHADOW_OUT);

	main_frame = gtk_frame_new("Project Details");

	vbox = gtk_vbox_new(FALSE, 10);

	widget = screen_mainwindow_control_overview_rerun(sc, pd);
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 0);

	widget = screen_mainwindow_control_overview_program_anatomy_new(sc, pd);
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 0);

	widget = screen_mainwindow_control_overview_thread_behavior_new();
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 0);

	widget = screen_mainwindow_control_overview_wait_and_block_new();
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 0);

	widget = screen_mainwindow_control_overview_ks_us_times_new();
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 0);

	widget = screen_mainwindow_control_overview_segment_sizes_new();
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 0);

	widget = screen_mainwindow_control_overview_function_sizes_new();
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 0);

	widget = screen_mainwindow_control_overview_systemcall_distribution_new();
	gtk_box_pack_start(GTK_BOX(vbox), widget, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(main_frame), vbox);
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scroll_widget), main_frame);

	return scroll_widget;
}



GtkWidget *screen_mainwindow_control_overview_new(struct studio_context *sc, gchar *path)
{
	if (path)
		return control_overview_path_new(sc, path);

	/* fallback, present something usefull if
	 * no project was selected */
	return control_overview_empty_new(sc, "No Project Selected");
}

