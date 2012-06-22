/*
 * studio-module-overview.c
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
#include "studio-module.h"

#define stringify_1(s...) #s
#define stringify(s...) stringify_1(s)

#define MARGIN_TOP    10
#define MARGIN_BOTTOM 10
#define MARGIN_LEFT   10
#define MARGIN_RIGHT  10

/* padding should be large enough to draw
 * axis labels */
#define COORDINATE_GRID_PADDING_LEFT   20
#define COORDINATE_GRID_PADDING_BOTTOM 20

enum {
        GRAPH_MODE_ABSOLUTE,
        GRAPH_MODE_OFFSETED,
        GRAPH_MODE_LOGSCALE
};

struct execution_time_history_data {
	time_t record_time;
	unsigned long long value;
};


struct mod_overview_priv {
	int foo;
	GList *execution_time_history_list;
};


static gboolean std_dev_widget_draw(GtkWidget *widget, GdkEventExpose *event, gpointer priv_data)
{
        bool first_segment = true;
	struct mod_overview_priv *ov;
	GList *list_tmp;
	unsigned long long val_min, val_max;
	unsigned values;
	cairo_t *cr;
	double w, h;
	unsigned long long data_height, scale_factor;
	int mode, data_gap_x, x_offset;


	assert(priv_data);

	mode = GRAPH_MODE_OFFSETED;

	cr = gdk_cairo_create(widget->window);
	//cairo_rectangle(cr, event->area.x, event->area.y, event->area.width, event->area.height);
	//cairo_clip(cr);

	w = event->area.width;
	h = event->area.height;

	fprintf(stderr, "width: %lf, height: %lf\n", w, h);

	val_min = ULLONG_MAX;
	val_max = 0;
	values = 0;

	ov = priv_data;

	list_tmp = ov->execution_time_history_list;
	while (list_tmp != NULL) {
		const struct execution_time_history_data *hd = list_tmp->data;
		val_max = MAX(val_max, hd->value);
		val_min = MIN(val_min, hd->value);
		fprintf(stderr, "new max: %llu, new min: %llu\n", val_max, val_min);
		values++;
		list_tmp = g_list_next(list_tmp);
	}


        // XXX: add check
        data_gap_x = (w - (MARGIN_LEFT + MARGIN_RIGHT + COORDINATE_GRID_PADDING_LEFT)) / (values - 1);

        /* draw reference and coordinate lines/grid */
	cairo_set_source_rgb(cr, .4, .4, .4);

        /* Y axis */
        cairo_move_to(cr, MARGIN_LEFT + COORDINATE_GRID_PADDING_LEFT, MARGIN_TOP);
        cairo_line_to(cr, MARGIN_LEFT + COORDINATE_GRID_PADDING_LEFT, h - (COORDINATE_GRID_PADDING_BOTTOM + MARGIN_BOTTOM) + 5);
        cairo_stroke(cr);

        /* X axis */
        cairo_move_to(cr, MARGIN_LEFT + COORDINATE_GRID_PADDING_LEFT - 5, h - (COORDINATE_GRID_PADDING_BOTTOM + MARGIN_BOTTOM));
        cairo_line_to(cr, w - MARGIN_RIGHT, h - (COORDINATE_GRID_PADDING_BOTTOM + MARGIN_BOTTOM));
        cairo_stroke(cr);

#if 0
        /* help lines */
        static const double dashed1[] = {4.0, 1.0};
        static int len1  = sizeof(dashed1) / sizeof(dashed1[0]);
        cairo_set_line_width(cr, 1.5);
        cairo_set_dash(cr, dashed1, len1, 0);
#endif


        switch (mode) {
        case GRAPH_MODE_ABSOLUTE:
                data_height = val_max;
                scale_factor = data_height;
                break;
        case GRAPH_MODE_OFFSETED:
                data_height = val_max - val_min;
                scale_factor = data_height;
                break;
        case GRAPH_MODE_LOGSCALE:
                data_height = val_max;
                scale_factor = data_height;
                break;
        default:
                assert(0);
                break;
        }


        /* draw actual data points */
        x_offset = 0 + MARGIN_LEFT + COORDINATE_GRID_PADDING_LEFT;
	list_tmp = ov->execution_time_history_list;
	while (list_tmp != NULL) {
		int y, screen_distance_to_y_0;
		const struct execution_time_history_data *hd = list_tmp->data;

                switch (mode) {
                case GRAPH_MODE_ABSOLUTE:
                        break;
                case GRAPH_MODE_OFFSETED:
                        y = ((double)h - (MARGIN_TOP + MARGIN_BOTTOM + COORDINATE_GRID_PADDING_BOTTOM)) / ((double)hd->value / scale_factor);
			fprintf(stderr, "y: %d\n", y);
                        screen_distance_to_y_0 = h - (MARGIN_BOTTOM + COORDINATE_GRID_PADDING_BOTTOM);
                        y = screen_distance_to_y_0 - y;
                        break;
                case GRAPH_MODE_LOGSCALE:
                        break;
		default:
			assert(0);
			break;
                }

                if (first_segment) {
                        cairo_move_to(cr, x_offset - 0.0001, y);
                        first_segment = false;
                }

		cairo_set_source_rgb(cr, (120./255), (173.0/255), (224./255));
		cairo_set_line_width (cr, 3.0);
                cairo_line_to(cr, x_offset, y);
		fprintf(stderr, "x_offset: %d, y: %d\n", x_offset, y);

                x_offset += data_gap_x;


#if 0
                /* draw cicrle */
                cairo_set_source_rgb(cr, 0.33, 0.33, 0.33);
                cairo_arc(cr, x_offset, y, 10, 0, 2 * M_PI);
                cairo_stroke_preserve(cr);

                cairo_set_source_rgb(cr, 0.44, 0.44, 0.44);
                cairo_fill(cr);
#endif


		list_tmp = g_list_next(list_tmp);
        }

	cairo_stroke(cr);

	cairo_destroy (cr);
	return TRUE;
}

#define STD_DEV_WIDGET_WIDTH 100
#define STD_DEV_WIDGET_HEIGHT 50

static gboolean wallclock_history_draw(GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
	cairo_t *cr;
	double wthird;

	(void) data;

	fprintf(stderr, "XXX: event exposed\n");

	/* get a cairo_t */
	cr = gdk_cairo_create(widget->window);

	cairo_rectangle(cr, event->area.x, event->area.y, event->area.width, event->area.height);
	cairo_clip (cr);

	cairo_set_line_width(cr, 1.5);
	cairo_set_source_rgb (cr, .3, .3, .3);

	wthird = event->area.width / 4;

	cairo_move_to(cr, 0, event->area.height - 2);
	cairo_curve_to(cr, wthird, event->area.height - 2, wthird, 0 + 2, event->area.width / 2, 0 + 2);
	cairo_curve_to(cr, event->area.width / 2 + wthird, 0 + 2, event->area.width/2 + wthird, event->area.height - 2, event->area.width, event->area.height - 2);
	cairo_close_path(cr);
	cairo_fill_preserve(cr);
	cairo_set_source_rgb (cr, .10, .10, .10);
	cairo_stroke(cr);

#if 0

	cairo_move_to(cr, 0, 50);
	cairo_set_source_rgb (cr, .50, .50, .0);
	cairo_line_to(cr, event->area.width, 50);
	cairo_stroke(cr);
#endif

	cairo_destroy (cr);


	return TRUE;
}


static GtkWidget *overview_execution_time(struct mod_overview_priv *ov)
{
	GtkWidget *expander;
	GtkWidget *label;
	GtkWidget *box;
	GtkWidget *vbox;
	GtkWidget *area;
	GtkWidget *hseparator;

	expander= gtk_expander_new("Execution Time");

	box = gtk_hbox_new(FALSE, 20);

	/* big time */
	label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label),
			"<span size=\"xx-large\" foreground=\"#888888\" font_weight=\"bold\">"
			"00:00:13.</span><span size=\"x-large\" foreground=\"#888888\""
			"font_weight=\"bold\">481959393</span>");
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);


	hseparator = gtk_vseparator_new();
	gtk_box_pack_start(GTK_BOX(box), hseparator, FALSE, FALSE, 0);

	/* all times */
	vbox = gtk_vbox_new(FALSE, 5);

	label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label),
			"<span font_desc=\"mono 10\" foreground=\"#888888\">"
			"Avg: 00:00:13</span>"
			"<span font_desc=\"mono 6\" foreground=\"#888888\">.481959393</span>");
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

	label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label),
			"<span font_desc=\"mono 10\" foreground=\"#888888\">"
			"Max: 00:00:13</span>"
			"<span font_desc=\"mono 6\" foreground=\"#888888\">.481959393</span>");
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

	label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label),
			"<span font_desc=\"mono 10\" foreground=\"#888888\">"
			"Min: 00:00:13</span>"
			"<span font_desc=\"mono 6\" foreground=\"#888888\">.481959393</span>");
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(box), vbox, FALSE, FALSE, 0);

	hseparator = gtk_vseparator_new();
	gtk_box_pack_start(GTK_BOX(box), hseparator, FALSE, FALSE, 0);


	/* gauss drawing */
	vbox = gtk_vbox_new(FALSE, 5);
	label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label),
			"<span foreground=\"#888888\">"
			"Probability Density</span>");
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

	area = gtk_drawing_area_new();
	gtk_widget_show(area);
	gtk_widget_set_app_paintable(GTK_WIDGET (area), TRUE);
	gtk_widget_set_size_request(area, STD_DEV_WIDGET_WIDTH, STD_DEV_WIDGET_HEIGHT);
	g_signal_connect(area, "expose-event", G_CALLBACK(wallclock_history_draw), ov);
	gtk_box_pack_start(GTK_BOX(vbox), area, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), vbox, FALSE, FALSE, 0);


	/* stuff is to the main */
	gtk_container_add(GTK_CONTAINER(expander), box);
	gtk_expander_set_expanded(GTK_EXPANDER(expander), TRUE);

	return expander;
}




static GtkWidget *overview_history(struct mod_overview_priv *ov)
{
	GtkWidget *expander;
	GtkWidget *notebook;
	GtkWidget *label;
	GtkWidget *frame;
	GtkWidget *area;

	(void) ov;

	expander= gtk_expander_new("Trend");

	notebook =  gtk_notebook_new();
	gtk_notebook_popup_enable(GTK_NOTEBOOK(notebook));
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_TOP);

	area = gtk_drawing_area_new();
	gtk_widget_set_size_request (area, 400, 300);
	g_signal_connect(area, "expose-event", G_CALLBACK(std_dev_widget_draw), ov);
	label = gtk_label_new(" Wallclock Time Trend");
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), area, label);

	frame = gtk_frame_new("instructions");
	label = gtk_label_new(" Instruction Trend ");
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), frame, label);

	frame = gtk_frame_new("LLC");
	label = gtk_label_new(" LLC Trend ");
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), frame, label);


	gtk_container_set_border_width(GTK_CONTAINER (frame), 2);
	gtk_widget_set_size_request (frame, 100, 75);


	gtk_container_add(GTK_CONTAINER(expander), notebook);
	gtk_expander_set_expanded(GTK_EXPANDER(expander), TRUE);

	return expander;
}



static GtkWidget *overview_widget_new(void *priv_data)
{
	GtkWidget *box;
	GtkWidget *frame;
	GtkWidget *hseparator;
	GtkWidget *scroll_widget;
	struct mod_overview_priv *ov;

	assert(priv_data);

	ov = priv_data;

	scroll_widget = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_widget),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll_widget),
					    GTK_SHADOW_OUT);

	box = gtk_vbox_new(FALSE, 20);

	/* execution time widget */
	frame = overview_execution_time(ov);
	gtk_box_pack_start(GTK_BOX(box), frame, FALSE, FALSE, 0);

	hseparator = gtk_hseparator_new();
	gtk_box_pack_start(GTK_BOX(box), hseparator, FALSE, FALSE, 0);



	/* execution time widget */
	frame = overview_history(ov);
	gtk_box_pack_start(GTK_BOX(box), frame, FALSE, FALSE, 0);


	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scroll_widget), box);
	gtk_widget_show_all(scroll_widget);

	return scroll_widget;
}

static void generate_fake_execution_time_history_data(struct mod_overview_priv *od)
{
	struct execution_time_history_data *hd;

	hd = g_malloc0(sizeof(*hd));
	hd->record_time = 100;
	hd->value = 49192494;
	od->execution_time_history_list = g_list_append(od->execution_time_history_list, hd);

	hd = g_malloc0(sizeof(*hd));
	hd->record_time = 101;
	hd->value = 40192494;
	od->execution_time_history_list = g_list_append(od->execution_time_history_list, hd);

	hd = g_malloc0(sizeof(*hd));
	hd->record_time = 200;
	hd->value = 48192494;
	od->execution_time_history_list = g_list_append(od->execution_time_history_list, hd);

	hd = g_malloc0(sizeof(*hd));
	hd->record_time = 50;
	hd->value = 38192494;
	od->execution_time_history_list = g_list_append(od->execution_time_history_list, hd);
}


void module_overview_init(struct studio_context *sc)
{
	int ret;
	struct module_spec module, *m;
	struct mod_overview_priv *od;

	memset(&module, 0, sizeof(module));
	m = &module;

	g_snprintf(m->name, MODULE_SPEC_MAX_NAME - 1, "Overview");
	g_snprintf(m->description, MODULE_SPEC_MAX_DESCRIPTION - 1, "Provides standard info");

	m->widget_new = overview_widget_new;

	/* allocate and setup module private data */
	od = g_malloc0(sizeof(*od));
	generate_fake_execution_time_history_data(od);
	m->priv_data = od;


	ret = register_module(sc, m);
	if (ret != true) {
		fprintf(stderr, "Cannot initialzie module\n");
		return;
	}

	return;
}


