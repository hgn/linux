#ifndef __STUDIO_MODULE_H
#define __STUDIO_MODULE_H

#include <gtk/gtk.h>

#include "builtin-studio.h"


bool register_module(struct studio_context *, struct module_spec *);

/* studio-module-example.c */
void module_example_init(struct studio_context *);


#endif /* __STUDIO_MODULE_H */
