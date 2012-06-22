/*
 * studio-control-pane.c
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


void control_pane_data_init(struct studio_context *sc)
{
	assert(sc);
	sc->control_pane_data_list = NULL;
}


struct control_pane_data *control_pane_data_by_name(struct studio_context *sc, const char *name)
{
	GList *tmp_list;

	assert(name);

	tmp_list = sc->control_pane_data_list;
	while (tmp_list != NULL) {
		struct control_pane_data *cpml;

		cpml = tmp_list->data;

		if (!strcmp(cpml->name, name))
			return cpml;

		tmp_list = g_list_next(tmp_list);
	}

	return NULL;
}


bool control_pane_register_module(struct studio_context *sc, struct module_spec *module_spec)
{
	struct control_pane_data *cpml;

	assert(sc);
	assert(module_spec);

	cpml = g_malloc0(sizeof(*cpml));
	cpml->name = g_strdup(module_spec->name);

	/* remeber widget specific function pointer */
	cpml->widget_new     = module_spec->widget_new;
	cpml->widget_destroy = module_spec->widget_destroy;

	cpml->priv_data = module_spec->priv_data;
	cpml->sc = sc;

	fprintf(stderr, "register module: %s\n", cpml->name);
	sc->control_pane_data_list = g_list_append(sc->control_pane_data_list, cpml);

	return true;
}


enum
{
	COLUMN = 0,
	NUM_COLS
} ;




void row_activated(GtkTreeView *treeview, GtkTreePath *path,
		   GtkTreeViewColumn *col, gpointer priv_data)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	struct studio_context *sc;
	GList *tmp_list;

	assert(priv_data);

	sc = priv_data;

	model = gtk_tree_view_get_model(treeview);

	if (gtk_tree_model_get_iter(model, &iter, path)) {
		gchar *name;
		gtk_tree_model_get(model, &iter, COLUMN, &name, -1);

		fprintf(stderr, "row actived: %s\n", name);

		tmp_list = sc->control_pane_data_list;
		while (tmp_list != NULL) {
			struct control_pane_data *cpml;

			cpml = tmp_list->data;
			fprintf(stderr, "IN %s\n", cpml->name);

			if (!strcmp(cpml->name, name)) {
				GtkWidget *w;
				fprintf(stderr, "select and found: %s\n", name);
				w = cpml->widget_new(cpml->priv_data);
				main_notebook_add_widget(sc, cpml, w);
			}


			tmp_list = g_list_next(tmp_list);
		}

		g_free(name);
	}
}




static GtkTreeModel *create_and_fill_model(struct studio_context *sc)
{
	GList *tmp;
	GtkTreeStore *treestore;
	GtkTreeIter toplevel, child;

	treestore = gtk_tree_store_new(NUM_COLS,
			G_TYPE_STRING);

	gtk_tree_store_append(treestore, &toplevel, NULL);
	gtk_tree_store_set(treestore, &toplevel, COLUMN, "Trace Based Analysis", -1);

	gtk_tree_store_append(treestore, &child, &toplevel);
	gtk_tree_store_set(treestore, &child,
			COLUMN, "Lock Contention",
			-1);

	gtk_tree_store_append(treestore, &toplevel, NULL);
	gtk_tree_store_set(treestore, &toplevel, COLUMN, "EBS Analysis", -1);

	gtk_tree_store_append(treestore, &child, &toplevel);
	gtk_tree_store_set(treestore, &child,
			COLUMN, "Pipeline Stalls",
			-1);

	gtk_tree_store_append(treestore, &child, &toplevel);
	gtk_tree_store_set(treestore, &child,
			COLUMN, "Frontent Stalls",
			-1);

	gtk_tree_store_append(treestore, &child, &toplevel);
	gtk_tree_store_set(treestore, &child,
			COLUMN, "Backend Stalls",
			-1);

	gtk_tree_store_append(treestore, &child, &toplevel);
	gtk_tree_store_set(treestore, &child,
			COLUMN, "CPU Time",
			-1);
	gtk_tree_store_append(treestore, &child, &toplevel);
	gtk_tree_store_set(treestore, &child,
			COLUMN, "CPI/IPC",
			-1);
	gtk_tree_store_append(treestore, &child, &toplevel);
	gtk_tree_store_set(treestore, &child,
			COLUMN, "Cache Behavior",
			-1);

	gtk_tree_store_append(treestore, &toplevel, NULL);
	gtk_tree_store_set(treestore, &toplevel, COLUMN, "Event Analysis", -1);

	gtk_tree_store_append(treestore, &child, &toplevel);
	gtk_tree_store_set(treestore, &child,
			COLUMN, "Scheduling Analysis",
			-1);

	gtk_tree_store_append(treestore, &child, &toplevel);
	gtk_tree_store_set(treestore, &child,
			COLUMN, "Priority Analysis",
			-1);

	gtk_tree_store_append(treestore, &child, &toplevel);
	gtk_tree_store_set(treestore, &child,
			COLUMN, "Wakeup Analysis",
			-1);

	tmp = sc->control_pane_data_list;
	while (tmp != NULL) {
		struct control_pane_data *cpml;

		cpml = tmp->data;

		gtk_tree_store_append(treestore, &child, &toplevel);
		gtk_tree_store_set(treestore, &child,
				COLUMN, cpml->name,
				-1);

		tmp = g_list_next(tmp);
	}

	return GTK_TREE_MODEL(treestore);
}



static GtkWidget *create_view_and_model(struct studio_context *sc)
{
	GtkTreeViewColumn *col;
	GtkCellRenderer *renderer;
	GtkWidget *view;
	GtkTreeModel *model;

	(void) sc;

	view = gtk_tree_view_new();

	col = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(col, "Analysis");
	gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(col, renderer, TRUE);
	gtk_tree_view_column_add_attribute(col, renderer, "text", COLUMN);

	model = create_and_fill_model(sc);
	gtk_tree_view_set_model(GTK_TREE_VIEW(view), model);
	g_object_unref(model);

	return view;
}



GtkWidget *control_pane_new(struct studio_context *sc)
{
	GtkWidget *scroll_widget;
	GtkWidget *view;
	GtkTreeSelection *selection;

	scroll_widget = gtk_scrolled_window_new(NULL, NULL);

	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_widget),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll_widget),
					    GTK_SHADOW_OUT);

	view = create_view_and_model(sc);
	g_signal_connect(view, "row-activated", G_CALLBACK(row_activated), sc);

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));

	//gtk_container_add(GTK_CONTAINER(scroll_widget), view);
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scroll_widget), view);

	return scroll_widget;
}
