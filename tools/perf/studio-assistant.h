#ifndef __STUDIO_ASSISTANT_H
#define __STUDIO_ASSISTANT_H


#include <gtk/gtk.h>

/* after assistent completion data is returned.
 * This data is here defined. Additionally
 * the widget is returned, which CAN/SHOULD
 * destroyed afterwards. Similar, the data must
 * be freed also */

struct studio_assitant_new_project_data {
	char *project_name; /* this argument comes before assistant is started and must be freed */
	char *executable_path;
	void (*cb)(struct studio_assitant_new_project_data *);
};

int assistant_new(void (*cb)(struct studio_assitant_new_project_data *), char *project_name);
void studio_assitant_new_project_data_free(struct studio_assitant_new_project_data *);




#endif	/* __STUDIO_ASSISTANT_H */
