#include "builtin-studio.h"

enum
{
	LIST_ITEM = 0,
	N_COLUMNS
};

GtkWidget *list;



static void screen_nootbook_status_init(GtkWidget *notebook)
{
	GtkWidget *frame;
	GtkWidget *label;
	GtkWidget *vbox;
	GdkPixbuf *image = NULL;
	GtkWidget *widget = NULL;

	vbox = gtk_vbox_new(FALSE, 0);

	image = load_pixbuf_from_file("lstopo.png");
	widget = gtk_image_new_from_pixbuf (image);


	frame = gtk_frame_new("Machine Memory Hierarchy");
	gtk_container_set_border_width(GTK_CONTAINER (frame), 2);
	gtk_widget_set_size_request (frame, 100, 75);

	gtk_container_add (GTK_CONTAINER (frame), widget);


	gtk_widget_show(widget);
	gtk_widget_show(frame);

	gtk_box_pack_start(GTK_BOX(vbox), frame, TRUE, TRUE, 0);
	gtk_widget_show(vbox);

	frame = gtk_frame_new("Status Page");
	gtk_container_set_border_width(GTK_CONTAINER (frame), 2);
	gtk_widget_set_size_request (frame, 100, 75);
	gtk_widget_show(frame);

	gtk_box_pack_start(GTK_BOX(vbox), frame, TRUE, TRUE, 0);
	gtk_widget_show(vbox);


	label = gtk_label_new ("Project History");
	gtk_container_add(GTK_CONTAINER (frame), label);
	gtk_widget_show(label);


	label = gtk_label_new("Project History");
	gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK (notebook), label, TRUE);
	gtk_notebook_append_page(GTK_NOTEBOOK (notebook), vbox, label);
	gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(notebook), vbox, TRUE);
}


static void init_list(GtkWidget *gtlist)
{

	GtkCellRenderer    *renderer;
	GtkTreeViewColumn  *column;
	GtkListStore       *store;

	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes("List Item",
			renderer, "text", LIST_ITEM, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW (gtlist), column);

	store = gtk_list_store_new (N_COLUMNS, G_TYPE_STRING);

	gtk_tree_view_set_model(GTK_TREE_VIEW (gtlist),
			GTK_TREE_MODEL(store));

	g_object_unref(store);
}

static void addd(void) {
	GtkListStore *store;
	GtkTreeIter  iter;

	const char *str = "fpp";

	store = GTK_LIST_STORE(gtk_tree_view_get_model(
				GTK_TREE_VIEW(list)));

	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter, LIST_ITEM, str, -1);
}

static GtkWidget *gt(struct studio_context *sc)
{
	GtkTreeSelection *selection;

	sc->screen.sw = gtk_scrolled_window_new(NULL, NULL);
	list = gtk_tree_view_new();


	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(sc->screen.sw),
			GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW(sc->screen.sw),
			GTK_SHADOW_ETCHED_IN);

	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (list), TRUE);


	gtk_container_add(GTK_CONTAINER (sc->screen.sw), list);

	init_list(list);
	addd();

	selection  = gtk_tree_view_get_selection(GTK_TREE_VIEW(list));
	(void) selection;


	gtk_widget_show_all(sc->screen.sw);

	return sc->screen.sw;

}

static void screen_nootbook_cbs_init(struct studio_context *sc, GtkWidget *notebook)
{
	GtkWidget *frame;
	GtkWidget *tab_container, *close_button, *tab_label;

	frame = gt(sc);

	tab_label = gtk_label_new("Counter Based Sampling");
	tab_container = gtk_hbox_new(FALSE, 3);
	close_button = gtk_button_new();

	gtk_button_set_relief(GTK_BUTTON(close_button), GTK_RELIEF_NONE);

	gtk_box_pack_start(GTK_BOX(tab_container), tab_label, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(tab_container), close_button, FALSE, FALSE, 0);


	gtk_container_add(GTK_CONTAINER(close_button), gtk_image_new_from_stock(GTK_STOCK_CLOSE, GTK_ICON_SIZE_SMALL_TOOLBAR));
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), frame, tab_container);

	gtk_widget_show_all(tab_container);
}


static void screen_nootbook_ebs_init(GtkWidget *notebook)
{
	GtkWidget *frame;
	GtkWidget *label;

	frame = gtk_frame_new("Counter Based Sampling");
	gtk_container_set_border_width (GTK_CONTAINER (frame), 2);
	gtk_widget_set_size_request (frame, 100, 75);
	gtk_widget_show (frame);

	label = gtk_label_new("Counter Based Sampling");
	gtk_container_add (GTK_CONTAINER (frame), label);
	gtk_widget_show (label);

	label = gtk_label_new("Event base sampling");
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), frame, label);
}


GtkWidget *screen_notebook_main_init(struct studio_context *sc)
{
	/* Create a new notebook, place the position of the tabs */
	sc->screen.main_paned_workspace = gtk_notebook_new();
	gtk_notebook_popup_enable(GTK_NOTEBOOK(sc->screen.main_paned_workspace));
	//gtk_notebook_set_homogeneous_tabs(GTK_NOTEBOOK(sc->screen.main_paned_workspace), TRUE);
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(sc->screen.main_paned_workspace), GTK_POS_BOTTOM);

	screen_nootbook_status_init(sc->screen.main_paned_workspace);
	screen_nootbook_cbs_init(sc, sc->screen.main_paned_workspace);
	screen_nootbook_ebs_init(sc->screen.main_paned_workspace);
	screen_nootbook_thread_analyzer_init(sc->screen.main_paned_workspace);

	gtk_notebook_set_current_page(GTK_NOTEBOOK(sc->screen.main_paned_workspace), 0);

	//gtk_box_pack_start(GTK_BOX(sc.screen.main_paned_workspace), sc.screen.main_paned_workspace, TRUE, TRUE, 0);
	gtk_widget_set_size_request(sc->screen.main_paned_workspace, 50, -1);

	return sc->screen.main_paned_workspace;
}

struct displayed_module {
	const char *name;
};

GList *displayed_module_list;

static gboolean remove_tab(gpointer data)
{
	struct control_pane_data *cpd = data;
	GList *tmp_list;

	if (cpd->widget_destroy)
		cpd->widget_destroy(cpd->priv_data);

	gtk_notebook_remove_page(GTK_NOTEBOOK(cpd->sc->screen.main_paned_workspace),
			         cpd->notebook_id);

	tmp_list = displayed_module_list;
	while (tmp_list != NULL) {
		struct displayed_module *dm = tmp_list->data;
		if (!strcmp(dm->name, cpd->name)) {
			displayed_module_list =
				g_list_remove_link(displayed_module_list, tmp_list);
			g_list_free_1(tmp_list);
			return TRUE;
		}
		tmp_list = g_list_next(tmp_list);
	}

	//assert(0);

	return TRUE;
}



bool main_notebook_add_widget(struct studio_context *sc,
		struct control_pane_data *cpd, GtkWidget *widget)
{
	struct displayed_module *dm;
	GtkWidget *tab_container, *close_button, *tab_label;
	GList *tmp_list;

	assert(sc);
	assert(cpd);
	assert(cpd->name);
	assert(widget);
	assert(sc->screen.main_paned_workspace);

	/* First check if the module is already displayed
	 * If so we return simple false to provide
	 * an indication
	 */
	tmp_list = displayed_module_list;
	while (tmp_list != NULL) {
		dm = tmp_list->data;
		if (!strcmp(dm->name, cpd->name)) {
			fprintf(stderr, "already displayed %s\n", cpd->name);
			return false;
		}
		tmp_list = g_list_next(tmp_list);
	}


	tab_label = gtk_label_new(cpd->name);
	tab_container = gtk_hbox_new(FALSE, 0);

	close_button = gtk_button_new();
	g_signal_connect_swapped(G_OBJECT(close_button),
			         "clicked",
				 G_CALLBACK(remove_tab),
				 cpd);

	gtk_button_set_relief(GTK_BUTTON(close_button), GTK_RELIEF_NONE);

	gtk_box_pack_start(GTK_BOX(tab_container), tab_label, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(tab_container), close_button, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(close_button),
			  gtk_image_new_from_stock(GTK_STOCK_CLOSE,
			  GTK_ICON_SIZE_SMALL_TOOLBAR));

	gtk_widget_show_all(tab_container);

	/* register the new module at the list,
	 * so that we know later what we display and what not
	 */
	dm = g_malloc0(sizeof(*dm));
	dm->name = cpd->name;
	displayed_module_list = g_list_append(displayed_module_list, dm);

	cpd->notebook_id = gtk_notebook_append_page(GTK_NOTEBOOK(sc->screen.main_paned_workspace),
			                            widget, tab_container);
	if (cpd->notebook_id == -1)
		return false;

	gtk_notebook_set_menu_label_text(GTK_NOTEBOOK(sc->screen.main_paned_workspace),
			                 widget, cpd->name);

	return true;
}
