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

#include "studio-module.h"
#include "studio-control-pane.h"


bool register_module(struct studio_context *sc, struct module_spec *module)
{
	int ret;

	assert(sc);
	assert(module);

	ret = control_pane_register_module(sc, module);
	if (!ret) {
		fprintf(stderr, "cannot add module\n");
		return false;
	}


	return true;
}
