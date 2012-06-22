/*
 * studio-status-widget.c
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

#include "studio-status-widget.h"

void studio_main_status_widget_new(struct studio_context *sc, GtkWidget *parent)
{
	GtkWidget *hbox;
	GtkWidget *label;

	hbox = gtk_hbox_new(FALSE, 0);

#if 0

	label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label),
			"<span size=\"x-large\" foreground=\"red\" font_weight=\"bold\"> Project: foo </span>");
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

#endif

	label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label),
			"<span size=\"x-large\" font_weight=\"normal\" foreground=\"#666\">Project: Fooo  </span>");
	gtk_box_pack_end(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(parent), hbox, FALSE, FALSE, 10);
	gtk_widget_show_all(parent);
}
