/*
 * studio-module.c
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

GtkWidget *example_widget_new(void *priv_data)
{
	GtkWidget *frame;

	frame = gtk_frame_new ("Counter Based Sampling");
	gtk_container_set_border_width (GTK_CONTAINER (frame), 2);
	gtk_widget_set_size_request (frame, 100, 75);
	gtk_widget_show (frame);

	return frame;
}

void module_example_init(struct studio_context *sc)
{
	int ret;
	struct module_spec module, *m;

	memset(&module, 0, sizeof(module));
	m = &module;

	g_snprintf(m->name, MODULE_SPEC_MAX_NAME - 1, "fooo");
	g_snprintf(m->description, MODULE_SPEC_MAX_DESCRIPTION - 1, "a example module");

	m->widget_new = example_widget_new;


	ret = register_module(sc, m);
	if (ret != true) {
		fprintf(stderr, "Cannot initialzie module\n");
		return;
	}

	return;
}


