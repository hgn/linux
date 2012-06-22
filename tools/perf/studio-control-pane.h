#ifndef __STUDIO_CONTROL_PANE_H
#define __STUDIO_CONTROL_PANE_H


#include <gtk/gtk.h>

#include "builtin-studio.h"


GtkWidget *control_pane_new(struct studio_context *sc);

void control_pane_data_init(struct studio_context *);
bool control_pane_register_module(struct studio_context *, struct module_spec *);






#endif /* __STUDIO_CONTROL_PANE_H */
