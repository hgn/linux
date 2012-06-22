/*
 * studio-assistant.c
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <math.h>
#include <time.h>


#include <libxml/parser.h>
#include <libxml/tree.h>

#include "perf.h"
#include "builtin.h"

#include "builtin-studio.h"
#include "studio-assistant.h"

#include <gtk/gtk.h>
#include <string.h>

static void entry_changed    (GtkEditable*, GtkAssistant*);
static void button_toggled   (GtkCheckButton*, GtkAssistant*);
static gboolean assistant_cancel (GtkAssistant*, gpointer);
static gboolean assistant_close  (GtkAssistant*, gpointer);

struct assistant_page_info {
	GtkWidget *widget;
	gint index;
	const gchar *title;
	GtkAssistantPageType type;
	gboolean complete;
};

enum {
	ASSISTENT_PAGE_ENTRY,
	ASSISTENT_PAGE_EXECUTABLE,
	ASSISTENT_PAGE_DETAILS,
};

static struct studio_assitant_new_project_data *studio_assitant_new_project_data_alloc(void)
{
	struct studio_assitant_new_project_data *pd;
	return g_malloc0(sizeof(*pd));
}

void studio_assitant_new_project_data_free(struct studio_assitant_new_project_data *pd)
{
	assert(pd);
	assert(pd->project_name);

	free(pd->project_name);

	g_free(pd->executable_path);
	g_free(pd);
}

static gboolean assistant_signal_cb(GtkAssistant *assistant, gpointer data)
{
	struct studio_assitant_new_project_data *pd = data;

	gtk_widget_destroy(GTK_WIDGET(assistant));

	pd->cb(pd);

	return true;
}

static gboolean assis_executable_run_filechoose(GtkWidget *widget, gpointer data)
{
	GtkWidget *dialog;
	GtkWidget *assistant;
	GtkWidget *entry;
	gchar *tmp;
	struct studio_assitant_new_project_data *pd;

	assistant = data;

	entry = g_object_get_data(G_OBJECT(assistant), "page-executable-entry");
	pd    = g_object_get_data(G_OBJECT(assistant), "project-data");

	dialog = gtk_file_chooser_dialog_new("Select executable",
					     GTK_WINDOW(assistant),
					     GTK_FILE_CHOOSER_ACTION_OPEN,
					     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					     GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
					     NULL);

	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), g_get_current_dir());

	if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT)
		goto out;

	tmp = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER (dialog));
	if (!tmp || strlen(tmp) <= 0)
		goto out;

	pd->executable_path = tmp;

	gtk_entry_set_text(entry, pd->executable_path);

	GtkWidget *page = gtk_assistant_get_nth_page (assistant, ASSISTENT_PAGE_EXECUTABLE);
	gtk_assistant_set_page_complete(assistant, page, TRUE);

out:
	gtk_widget_destroy(dialog);
}


static GtkWidget *assis_executable(GtkWidget *assistant)
{
	GtkWidget *vbox;
	GtkWidget *button;
	GtkWidget *entry;

	vbox = gtk_vbox_new(FALSE, 5);



	button = gtk_button_new_with_label("Select executable");
	g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(assis_executable_run_filechoose), assistant);

	gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 0);


	entry = gtk_entry_new();
	g_object_set_data(G_OBJECT(assistant), "page-executable-entry", (gpointer) entry);
	gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 0);

	return vbox;
}


int assistant_new(void (*cb)(struct studio_assitant_new_project_data *), char *project_name)
{
	GtkWidget *assistant, *entry, *label, *progress, *hbox;
	//GdkPixbuf *pixbuf;
	struct studio_assitant_new_project_data *pd;
	guint i;

	struct assistant_page_info page[] = {
		{ NULL, -1, "Create Perf Project",     GTK_ASSISTANT_PAGE_INTRO,    TRUE},
		{ NULL, -1, "Select Executable",       GTK_ASSISTANT_PAGE_CONTENT,  FALSE},
		{ NULL, -1, NULL,                      GTK_ASSISTANT_PAGE_CONTENT,  FALSE},
		{ NULL, -1, "Click the Check Button",  GTK_ASSISTANT_PAGE_CONTENT,  FALSE},
		{ NULL, -1, "Confirmation",            GTK_ASSISTANT_PAGE_CONFIRM,  TRUE},
	};

	/* Create a new assistant widget with no pages. */
	assistant = gtk_assistant_new();

	pd = studio_assitant_new_project_data_alloc();
	pd->project_name = project_name;
	pd->cb = cb;


	//pixbuf = load_pixbuf_from_file("asm-assistant.png");



	g_object_set_data(G_OBJECT(assistant), "project-data", (gpointer) pd);

	g_object_set_data(G_OBJECT(assistant), "pages", (gpointer) page);

	gtk_window_set_position((GtkWindow *)assistant, GTK_WIN_POS_CENTER);

	gtk_widget_set_size_request (assistant, 450, 300);
	gtk_window_set_title (GTK_WINDOW (assistant), "GtkAssistant Example");

	//g_signal_connect(G_OBJECT (assistant), "destroy", G_CALLBACK(cb), pd);

	page[ASSISTENT_PAGE_ENTRY].widget = gtk_label_new ("This is an example of a GtkAssistant. By\n"\
			"clicking the forward button, you can continue\n"\
			"to the next section!");
	page[ASSISTENT_PAGE_EXECUTABLE].widget = assis_executable(assistant);
	page[ASSISTENT_PAGE_DETAILS].widget = gtk_hbox_new(FALSE, 5);
	page[3].widget = gtk_check_button_new_with_label ("Click Me To Continue!");
	page[4].widget = gtk_label_new ("Text has been entered in the label and the\n"\
			"combo box is clicked. If you are done, then\n"\
			"it is time to leave!");

	/* Create the necessary widgets for the second page. */
	label = gtk_label_new ("Your Name: ");
	entry = gtk_entry_new ();
	gtk_box_pack_start(GTK_BOX(page[ASSISTENT_PAGE_DETAILS].widget), label, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(page[ASSISTENT_PAGE_DETAILS].widget), entry, FALSE, FALSE, 5);


	/* Add five pages to the GtkAssistant dialog. */
	for (i = 0; i < ARRAY_SIZE(page); i++)
	{
		page[i].index = gtk_assistant_append_page (GTK_ASSISTANT (assistant), page[i].widget);
		//gtk_assistant_set_page_side_image(assistant,  page[i].widget,  pixbuf);
		gtk_assistant_set_page_title (GTK_ASSISTANT (assistant), page[i].widget, page[i].title);
		gtk_assistant_set_page_type (GTK_ASSISTANT (assistant), page[i].widget, page[i].type);

		/* Set the introduction and conclusion pages as complete so they can be
		 * incremented or closed. */
		gtk_assistant_set_page_complete(GTK_ASSISTANT (assistant), page[i].widget, page[i].complete);
	}

	/* Update whether pages 2 through 4 are complete based upon whether there is
	 * text in the GtkEntry, the check button is active, or the progress bar
	 * is completely filled. */
	g_signal_connect (G_OBJECT (entry), "changed", G_CALLBACK (entry_changed), (gpointer) assistant);
	g_signal_connect (G_OBJECT (page[3].widget), "toggled", G_CALLBACK (button_toggled), (gpointer) assistant);
	g_signal_connect (G_OBJECT (assistant), "cancel", G_CALLBACK (assistant_cancel), pd);
	g_signal_connect (G_OBJECT (assistant), "close", G_CALLBACK (assistant_signal_cb), pd);

	gtk_widget_show_all(assistant);

	return 0;
}

/* If there is text in the GtkEntry, set the page as complete. Otherwise,
 * stop the user from progressing the next page. */
static void entry_changed(GtkEditable *entry, GtkAssistant *assistant)
{
	const gchar *text;
	struct studio_assitant_new_project_data *pd;

	GtkWidget *page = gtk_assistant_get_nth_page(assistant, ASSISTENT_PAGE_DETAILS);
	gtk_assistant_set_page_complete(assistant, page, true);


	return;


	pd = g_object_get_data(G_OBJECT(assistant), "project-data");
	if (pd->executable_path)
		g_free(pd->executable_path);

	text = gtk_entry_get_text(GTK_ENTRY(entry));

	pd->executable_path = g_strdup(text);

	gint num = gtk_assistant_get_current_page (assistant);
}

/* If the check button is toggled, set the page as complete. Otherwise,
 * stop the user from progressing the next page. */
static void button_toggled (GtkCheckButton *toggle, GtkAssistant *assistant)
{
	gboolean active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (toggle));
	gtk_assistant_set_page_complete (assistant, GTK_WIDGET (toggle), active);
}


/* If the dialog is cancelled, delete it from memory and then clean up after
 * the Assistant structure. */
static gboolean assistant_cancel(GtkAssistant *assistant, gpointer data)
{
	fprintf(stderr, "XXX\n");
	gtk_widget_destroy(GTK_WIDGET(assistant));

	return false;
}

/* This function is where you would apply the changes and destroy the assistant. */
static gboolean assistant_close(GtkAssistant *assistant, gpointer data)
{

	return false;
}
