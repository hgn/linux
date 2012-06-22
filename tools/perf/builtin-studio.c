/*
 * builtin-studio.c
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

#include <sys/utsname.h>
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

#include <glib.h>
#include <glib/gstdio.h>

#include "perf.h"
#include "builtin.h"
#include "util/util.h"
#include "util/hist.h"
#include "util/evlist.h"
#include "util/cache.h"
#include "util/evsel.h"
#include "util/sort.h"
#include "util/strlist.h"
#include "util/strfilter.h"
#include "util/symbol.h"
#include "util/debug.h"
#include "util/debugfs.h"
#include "util/parse-options.h"
#include "util/probe-finder.h"
#include "util/probe-event.h"
#include "util/exec_cmd.h"

#include "builtin-studio.h"
#include "studio-db.h"
#include "studio-assistant.h"
#include "studio-utils.h"
#include "studio-status-widget.h"
#include "studio-module.h"

#define MAX_COLUMNS                       32

#define DEFAULT_VAR_FILTER "!__k???tab_* & !__crc_*"
#define DEFAULT_FUNC_FILTER "!_*"

enum {
	THEME_COLOR_DARK,
	THEME_COLOR_LIGHT,
};

#define THEME_COLOR_STANDARD THEME_COLOR_LIGHT



struct studio_context sc;

static void screen_err_msg(const gchar *format, ...);
static void control_window_reload_new_project(struct studio_context *, gchar *path);



enum {
	NO_STATE = 0,
	BACKGROUND_PROCESS_PREPARE_TO_CREATE,
	BACKGROUND_PROCESS_STATE_INITIALIZE,
	BACKGROUND_PROCESS_STATE_START,
	BACKGROUND_PROCESS_STATE_FINISHED,
};


struct background_process_worker
{
	GtkWidget *label;
	GtkWidget *progress_bar;
	/* first "Start analysis" then "OK" */
	GtkWidget *control_button;
	gulong control_button_id;

	int state;

	GThread *gthread;
};


/* Session management structure */
static struct {
	bool list_events;
	bool force_add;
	bool show_lines;
	bool show_ext_vars;
	bool mod_events;
	int nevents;
	struct perf_probe_event events[MAX_PROBES];
	struct strlist *dellist;
	struct line_range line_range;
	const char *target;
	int max_probe_points;
	struct strfilter *filter;
} params;

/* Parse an event definition. Note that any error must die. */
static int parse_probe_event(const char *str)
{
	struct perf_probe_event *pev = &params.events[params.nevents];
	int ret;

	pr_debug("probe-definition(%d): %s\n", params.nevents, str);
	if (++params.nevents == MAX_PROBES) {
		pr_err("Too many probes (> %d) were specified.", MAX_PROBES);
		return -1;
	}

	/* Parse a perf-probe command into event */
	ret = parse_perf_probe_command(str, pev);
	pr_debug("%d arguments\n", pev->nargs);

	return ret;
}

static int parse_probe_event_argv(int argc, const char **argv)
{
	int i, len, ret;
	char *buf;

	/* Bind up rest arguments */
	len = 0;
	for (i = 0; i < argc; i++)
		len += strlen(argv[i]) + 1;
	buf = zalloc(len + 1);
	if (buf == NULL)
		return -ENOMEM;
	len = 0;
	for (i = 0; i < argc; i++)
		len += sprintf(&buf[len], "%s ", argv[i]);
	params.mod_events = true;
	ret = parse_probe_event(buf);
	free(buf);
	return ret;
}

static int opt_add_probe_event(const struct option *opt __used,
			      const char *str, int unset __used)
{
	if (str) {
		params.mod_events = true;
		return parse_probe_event(str);
	} else
		return 0;
}

static int opt_del_probe_event(const struct option *opt __used,
			       const char *str, int unset __used)
{
	if (str) {
		params.mod_events = true;
		if (!params.dellist)
			params.dellist = strlist__new(true, NULL);
		strlist__add(params.dellist, str);
	}
	return 0;
}

#ifdef DWARF_SUPPORT
static int opt_show_lines(const struct option *opt __used,
			  const char *str, int unset __used)
{
	int ret = 0;

	if (!str)
		return 0;

	if (params.show_lines) {
		pr_warning("Warning: more than one --line options are"
			   " detected. Only the first one is valid.\n");
		return 0;
	}

	params.show_lines = true;
	ret = parse_line_range_desc(str, &params.line_range);
	INIT_LIST_HEAD(&params.line_range.line_list);

	return ret;
}

static int opt_show_vars(const struct option *opt __used,
			 const char *str, int unset __used)
{
	struct perf_probe_event *pev = &params.events[params.nevents];
	int ret;

	if (!str)
		return 0;

	ret = parse_probe_event(str);
	if (!ret && pev->nargs != 0) {
		pr_err("  Error: '--vars' doesn't accept arguments.\n");
		return -EINVAL;
	}

	return ret;
}
#endif


static const char * const probe_usage[] = {
	"perf probe [<options>] 'PROBEDEF' ['PROBEDEF' ...]",
	"perf probe [<options>] --add 'PROBEDEF' [--add 'PROBEDEF' ...]",
	"perf probe [<options>] --del '[GROUP:]EVENT' ...",
	"perf probe --list",
#ifdef DWARF_SUPPORT
	"perf probe [<options>] --line 'LINEDESC'",
	"perf probe [<options>] --vars 'PROBEPOINT'",
#endif
	NULL
};

static const struct option options[] = {
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show parsed arguments, etc)"),
	OPT_BOOLEAN('l', "list", &params.list_events,
		    "list up current probe events"),
	OPT_CALLBACK('d', "del", NULL, "[GROUP:]EVENT", "delete a probe event.",
		opt_del_probe_event),
	OPT_CALLBACK('a', "add", NULL,
#ifdef DWARF_SUPPORT
		"[EVENT=]FUNC[@SRC][+OFF|%return|:RL|;PT]|SRC:AL|SRC;PT"
		" [[NAME=]ARG ...]",
#else
		"[EVENT=]FUNC[+OFF|%return] [[NAME=]ARG ...]",
#endif
		"probe point definition, where\n"
		"\t\tGROUP:\tGroup name (optional)\n"
		"\t\tEVENT:\tEvent name\n"
		"\t\tFUNC:\tFunction name\n"
		"\t\tOFF:\tOffset from function entry (in byte)\n"
		"\t\t%return:\tPut the probe at function return\n"
#ifdef DWARF_SUPPORT
		"\t\tSRC:\tSource code path\n"
		"\t\tRL:\tRelative line number from function entry.\n"
		"\t\tAL:\tAbsolute line number in file.\n"
		"\t\tPT:\tLazy expression of line code.\n"
		"\t\tARG:\tProbe argument (local variable name or\n"
		"\t\t\tkprobe-tracer argument format.)\n",
#else
		"\t\tARG:\tProbe argument (kprobe-tracer argument format.)\n",
#endif
		opt_add_probe_event),
	OPT_BOOLEAN('f', "force", &params.force_add, "forcibly add events"
		    " with existing name"),
#ifdef DWARF_SUPPORT
	OPT_CALLBACK('L', "line", NULL,
		     "FUNC[:RLN[+NUM|-RLN2]]|SRC:ALN[+NUM|-ALN2]",
		     "Show source code lines.", opt_show_lines),
	OPT_CALLBACK('V', "vars", NULL,
		     "FUNC[@SRC][+OFF|%return|:RL|;PT]|SRC:AL|SRC;PT",
		     "Show accessible variables on PROBEDEF", opt_show_vars),
	OPT_BOOLEAN('\0', "externs", &params.show_ext_vars,
		    "Show external variables too (with --vars only)"),
	OPT_STRING('k', "vmlinux", &symbol_conf.vmlinux_name,
		   "file", "vmlinux pathname"),
	OPT_STRING('s', "source", &symbol_conf.source_prefix,
		   "directory", "path to kernel source"),
	OPT_STRING('m', "module", &params.target,
		   "modname|path",
		   "target module name (for online) or path (for offline)"),
#endif
	OPT__DRY_RUN(&probe_event_dry_run),
	OPT_INTEGER('\0', "max-probes", &params.max_probe_points,
		 "Set how many probe points can be found for a probe."),
	OPT_END()
};






static int studio_init(void)
{
	int ret;
	const char *exec_path;
	struct passwd *pw = getpwuid(getuid());
	char image_path[256];

	if (!pw || !pw->pw_dir)
		return -EINVAL;

	memset(&sc, 0, sizeof(sc));

	sc.homedirpath = strdup(pw->pw_dir);
	if (!sc.homedirpath)
		return -ENOMEM;

	pr_debug("Home directory: %s\n", sc.homedirpath);

	exec_path = perf_exec_path();
	if (!exec_path) {
		pr_err("No execpath available\n");
		return -EINVAL;
	}

	switch (sc.screen.theme) {
	case THEME_COLOR_DARK:
		sc.pixmapdir = g_strdup_printf("%s/%s", exec_path, "share/pixmaps/dark/");
		sc.buttondir = g_strdup_printf("%s/%s", exec_path, "share/pixmaps/buttons/16x16/");
		break;
	default:
		sc.pixmapdir = g_strdup_printf("%s/%s", exec_path, "share/pixmaps/light/");
		sc.buttondir = g_strdup_printf("%s/%s", exec_path, "share/pixmaps/buttons/16x16/");
		break;
	}

	if (!sc.pixmapdir || !sc.buttondir)
		return -ENOMEM;

	/* make a access test */
	ret = snprintf(image_path, sizeof(image_path), "%s%s", sc.pixmapdir, "back.png");
	if (ret < (int)strlen("back.png")) {
		pr_err("Cannot construct path to pixmap images\n");
		return -EINVAL;
	}

	if (access(image_path, F_OK)) {
		pr_err("Image directory seems empty: %s\n", sc.pixmapdir);
		return -EINVAL;
	}


	pr_err("Pixmaps path: %s\n", sc.pixmapdir);
	pr_err("Button path:  %s\n", sc.buttondir);

	return 0;
}




/*********************************************************************************
 *
 *   Schart Widget
 */

/*
 * see http://xdump.org/reading-cutter/d1/d8b/cut-cairo-pie-chart_8h_source.html
 * for a nice example how to generate GTK objects
 */


GType schart_get_type (void) G_GNUC_CONST;
#define TYPE_SCHART		(schart_get_type())
#define SCHART(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_GPAPH, Schart))
#define SCHART_CLASS(obj)	(G_TYPE_CHECK_CLASS_CAST ((obj), SCHART, SchartClass))
#define IS_SCHART(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_SCHART))
#define IS_SCHART_CLASS(obj)	(G_TYPE_CHECK_CLASS_TYPE ((obj), EFF_TYPE_SCHART))
#define SCHART_GET_CLASS	(G_TYPE_INSTANCE_GET_CLASS ((obj), TYPE_SCHART, SchartClass))

typedef struct _Schart		Schart;
typedef struct _SchartClass	SchartClass;

enum {
	INTRA_EVENT,
	INTER_EVENT,
	INTRA_DURATION
};

struct schart_event {
	double time;
};

struct schart_data_raw_events {
	unsigned int event;
	unsigned int event_size;
	struct schart_event *schart_event;
};

struct schart_data_raw_cpu {
	unsigned int cpu;
	GSList *events;
};

struct schart_data {
	double time_start;
	double time_end;
	unsigned charts_no;
};

enum {
	DISPLAY_MODE_PEAKS = 0,
	DISPLAY_MODE_CURVES,

	DISPLAY_MODE_MAX
};

struct _Schart
{
	GtkDrawingArea parent;

	/* < private > */

	double zoom_factor;

	/* if -1 then the pointer is not
	 * in a visable area */
	gint pointer_x, pointer_y;
	void (*pointer_change_cb)(GtkWidget *, double);
	GtkWidget *pointer_change_data;

	gint width;
	gint height;

	int display_mode;

	struct schart_data schart_data;
	//struct schart_data_raw *schart_data_raw;
	GSList *schart_data_raw_list;
};

struct _SchartClass
{
	GtkDrawingAreaClass parent_class;
};

/* Public API */

GtkWidget *schart_new(void);

void schart_zoom_in(GtkWidget *w);
void schart_zoom_out(GtkWidget *w);

void schart_display_mode(GtkWidget *w, int mode);

/* Data API */
enum {
	EVENT_TYPE_MALLOC = 0,
	EVENT_TYPE_FUTEX,
	EVENT_TYPE_FOO,

	EVENT_TYPE_MAX,
};


void schart_set_data_time_start(GtkWidget *g, double t);
void schart_set_data_time_end(GtkWidget *g, double t);
void schart_set_data_time_start_end(GtkWidget *g, double start, double end);

void schart_set_data_charts_no(GtkWidget *g, unsigned charts_no);

void schart_register_pointer_change_cb(GtkWidget *g, void (*fn)(GtkWidget *, double), GtkWidget *);
void schart_set_raw_data(GtkWidget *g, GSList *);

/* header ends */

#define	ZOOM_FACTOR_DEFAULT 1.0
#define CORE_TIMELINE_HEIGHT 50


G_DEFINE_TYPE(Schart, schart, GTK_TYPE_DRAWING_AREA);

static gboolean schart_expose (GtkWidget *schart, GdkEventExpose *event);

static void schart_class_init(SchartClass *class)
{
	GtkWidgetClass *widget_class;
	widget_class = GTK_WIDGET_CLASS (class);
	widget_class->expose_event = schart_expose;
}


static void schart_init(Schart *schart)
{
	(void) schart;
}


static void draw_background(GtkWidget *widget, cairo_t *cr)
{
	cairo_set_source_rgb(cr, 0.3, 0.3, 0.3);
	cairo_set_line_width(cr, 0);
	cairo_rectangle(cr, 0, 0, widget->allocation.width, widget->allocation.height);
	cairo_fill(cr);
}



#define	STATUS_BOX_WIDTH 200

#define	TIMELINE_MARGIN_FROM_TOP 20
#define	TIMELINE_MARGIN_FROM_RIGHT 10
#define	TIMELINE_TICK_LEN 3

#define	CHART_HEIGHT 70
#define CHART_MARGIN 10



static double pixel_to_second(GtkWidget *widget, gint pos)
{
	double faktor;
	Schart *g = (Schart *)widget;

	if (pos <= STATUS_BOX_WIDTH)
		return -1.0l;

	faktor = ((double)g->schart_data.time_end - g->schart_data.time_start) /
		        (widget->allocation.width - STATUS_BOX_WIDTH - TIMELINE_MARGIN_FROM_RIGHT);

	return ((double)pos) * faktor + g->schart_data.time_start;
}


static int second_to_pixel(GtkWidget *widget, double xtime)
{
	double faktor;
	Schart *g = (Schart *)widget;

	if (xtime < g->schart_data.time_start)
		return -1;
	if (xtime > g->schart_data.time_end)
		return -1;

	faktor = ((double)g->schart_data.time_end - g->schart_data.time_start) /
		        (widget->allocation.width - STATUS_BOX_WIDTH - TIMELINE_MARGIN_FROM_RIGHT);

	return (xtime - g->schart_data.time_start) / faktor + STATUS_BOX_WIDTH;
}

static void draw_charts_background(GtkWidget *widget, cairo_t *cr)
{
	unsigned int i;
	Schart *g = (Schart *)(widget);

	int offset = TIMELINE_MARGIN_FROM_TOP + 20;


	for (i = 0; i < g->schart_data.charts_no; i++) {
		cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
		cairo_set_line_width(cr, 1);
		cairo_rectangle(cr, STATUS_BOX_WIDTH, offset,
				widget->allocation.width - TIMELINE_MARGIN_FROM_RIGHT - STATUS_BOX_WIDTH, CHART_HEIGHT);
		cairo_fill(cr);

		if (g->display_mode == DISPLAY_MODE_CURVES) {

			cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
			cairo_set_line_width(cr, 1);

			/* 100 % */
			cairo_move_to(cr, STATUS_BOX_WIDTH - 2, offset);
			cairo_line_to(cr, widget->allocation.width - TIMELINE_MARGIN_FROM_RIGHT, offset);
			cairo_stroke(cr);

			/* 75 % */
			cairo_move_to(cr, STATUS_BOX_WIDTH - 2, offset + ((float)CHART_HEIGHT * .25));
			cairo_line_to(cr, widget->allocation.width - TIMELINE_MARGIN_FROM_RIGHT, offset + ((float)CHART_HEIGHT * .25));
			cairo_stroke(cr);

			/* 50 % */
			cairo_move_to(cr, STATUS_BOX_WIDTH - 2, offset + ((float)CHART_HEIGHT * .5));
			cairo_line_to(cr, widget->allocation.width - TIMELINE_MARGIN_FROM_RIGHT, offset + ((float)CHART_HEIGHT * .5));
			cairo_stroke(cr);

			/* 25 % */
			cairo_move_to(cr, STATUS_BOX_WIDTH - 2, offset + ((float)CHART_HEIGHT * .75));
			cairo_line_to(cr, widget->allocation.width - TIMELINE_MARGIN_FROM_RIGHT, offset + ((float)CHART_HEIGHT * .5));
			cairo_stroke(cr);

			/* 0 % */
			cairo_move_to(cr, STATUS_BOX_WIDTH - 2, offset + CHART_HEIGHT);
			cairo_line_to(cr, widget->allocation.width - TIMELINE_MARGIN_FROM_RIGHT, offset + CHART_HEIGHT);
			cairo_stroke(cr);
		}

		offset += CHART_HEIGHT + CHART_MARGIN;
	}

	g->height = offset;
}

static void draw_cpu_pictograms(GtkWidget *widget, cairo_t *cr)
{

	unsigned int i, offset;
	gchar *text;
	Schart *g = (Schart *)(widget);

	offset = TIMELINE_MARGIN_FROM_TOP + 20;

	//cairo_select_font_face(cr, "Courier", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, 12);

	for (i = 0; i < g->schart_data.charts_no; i++) {

		cairo_set_line_width(cr, 2);

		cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
		cairo_rectangle(cr, 20, offset, STATUS_BOX_WIDTH - 20 - 20, CHART_HEIGHT);
		cairo_stroke(cr);

		cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
		cairo_rectangle(cr, 20, offset, STATUS_BOX_WIDTH - 20 - 20, CHART_HEIGHT);
		cairo_fill(cr);

		cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
		cairo_move_to(cr, 40, offset + 30);
		text = g_strdup_printf("CPU %d", i);
		cairo_show_text(cr, text);
		g_free(text);

		offset += CHART_HEIGHT + CHART_MARGIN;
	}
}


static void draw_charts(GtkWidget *widget, cairo_t *cr)
{
	unsigned int i;
	int all_events, offset;
	GSList *tmp;
	Schart *g;

	g = (Schart *)(widget);

	all_events = 0;
	offset = TIMELINE_MARGIN_FROM_TOP + 20;

	cairo_set_line_width(cr, 1);

	tmp = g->schart_data_raw_list;

	while (tmp != NULL) {

		int y_start;
		GSList *tmp2;
		struct schart_data_raw_cpu *schart_data_raw_cpu;

		schart_data_raw_cpu = tmp->data;
		y_start = schart_data_raw_cpu->cpu * (CHART_HEIGHT + CHART_MARGIN) + offset + 10;

		tmp2 = schart_data_raw_cpu->events;

		while (tmp2 != NULL) {
			struct schart_data_raw_events *schart_data_raw_events = tmp2->data;
			switch (schart_data_raw_events->event) {
				case EVENT_TYPE_MALLOC:
					cairo_set_source_rgb(cr, .99, .0, .0);
					break;
				case EVENT_TYPE_FUTEX:
					cairo_set_source_rgb(cr, .0, .0, .99);
					break;
				case EVENT_TYPE_FOO:
					cairo_set_source_rgb(cr, .0, .99, .0);
					break;
				default:
					abort();
					break;
			};

			for (i = 0; i < schart_data_raw_events->event_size; i++) {
				double xtime = schart_data_raw_events->schart_event[i].time;
				int x = second_to_pixel(widget, xtime);
				cairo_move_to(cr, x, y_start);
				cairo_line_to(cr, x, y_start + CHART_HEIGHT - 20);
				cairo_stroke(cr);

				all_events++;
			}

			tmp2 = g_slist_next(tmp2);
		}

		tmp = g_slist_next(tmp);
	}

	//fprintf(stderr, "display %d events\n", all_events);
}


static void draw_curves(GtkWidget *widget, cairo_t *cr)
{
	int offset, chunk_boundary_start, chunk_boundary_end, chunk_boundary_step;
	unsigned int i;
	long *chunk_cont;
	GSList *tmp;
	Schart *g = (Schart *)(widget);

	cairo_set_line_width(cr, 1);

	offset = TIMELINE_MARGIN_FROM_TOP + 20;

	tmp = g->schart_data_raw_list;


#define CLUSTER_SIZE 120
	chunk_boundary_start = second_to_pixel(widget, g->schart_data.time_start);
	chunk_boundary_end   = second_to_pixel(widget, g->schart_data.time_end);
	chunk_boundary_step  = ceil(((double)chunk_boundary_end - chunk_boundary_start) / CLUSTER_SIZE);

	chunk_cont = malloc(sizeof(long) * CLUSTER_SIZE);



	while (tmp != NULL) {

		GSList *tmp2;

		struct schart_data_raw_cpu *schart_data_raw_cpu = tmp->data;

		/* pre calculation */


		tmp2 = schart_data_raw_cpu->events;
		while (tmp2 != NULL) {

			int current_container, boundary_next, x, y;
			long max_val, factor;
			struct schart_data_raw_events *schart_data_raw_events;

			/* reset container */
			for (i = 0; i < CLUSTER_SIZE; i++) {
				chunk_cont[i] = 0;
			}

			schart_data_raw_events = tmp2->data;

			switch (schart_data_raw_events->event) {
				case EVENT_TYPE_MALLOC:
					cairo_set_source_rgb(cr, (0), (51.0/255), (102./255));
					//cairo_set_source_rgba(cr, .99, .0, .0, alpha);
					break;
				case EVENT_TYPE_FUTEX:
					cairo_set_source_rgb(cr, (51./255), (102.0/255), (153./255));
					//cairo_set_source_rgba(cr, .0, .0, .99, alpha);
					break;
				case EVENT_TYPE_FOO:
					cairo_set_source_rgb(cr, (102./255), (153.0/255), (204./255));
					//cairo_set_source_rgba(cr, .0, .99, .0, alpha);
					break;
				default:
					abort();
					break;
			};


			current_container = 0;
			boundary_next = chunk_boundary_start + chunk_boundary_step;


			for (i = 0; i < schart_data_raw_events->event_size; i++) {
				double xtime;
				int xf;

				xtime = schart_data_raw_events->schart_event[i].time;
				xf = second_to_pixel(widget, xtime);

				if (xf > boundary_next) {
					current_container++;
					boundary_next += chunk_boundary_step;
				}

				chunk_cont[current_container] += 1;
			}

			max_val = 0;

			for (i = 0; i < CLUSTER_SIZE; i++)
				max_val = MAX(max_val, chunk_cont[i]);

			factor = (double)CHART_HEIGHT / max_val;

			cairo_set_line_width(cr, 1);

			x = chunk_boundary_start;
			y = schart_data_raw_cpu->cpu * (CHART_HEIGHT + CHART_MARGIN) + offset + 70;

			cairo_move_to(cr, x, y);

			for (i = 0; i < CLUSTER_SIZE; i++) {

				cairo_line_to(cr, x + (chunk_boundary_step / 2), y - chunk_cont[i] * factor);
				x += chunk_boundary_step;
			}

			cairo_set_antialias(cr, CAIRO_ANTIALIAS_FAST);
			cairo_line_to(cr, chunk_boundary_end, y - 0 * factor);
			cairo_close_path(cr);
			cairo_fill_preserve(cr);
			cairo_set_source_rgba(cr, .99, .99, .99, 0.99);
			cairo_stroke (cr);

			//alpha = 0.4;

			tmp2 = g_slist_next(tmp2);
		}

		tmp = g_slist_next(tmp);
	}
}


static void draw_timeline(GtkWidget *widget, cairo_t *cr)
{
	int i, ticks;
	Schart *g = (Schart *)(widget);

	/* draw simple line */
	cairo_set_line_width(cr, 2);
	cairo_set_source_rgb (cr, .0, .0, .0);
	cairo_move_to(cr, STATUS_BOX_WIDTH, TIMELINE_MARGIN_FROM_TOP);
	cairo_line_to(cr, widget->allocation.width - TIMELINE_MARGIN_FROM_RIGHT, TIMELINE_MARGIN_FROM_TOP);
	cairo_stroke(cr);

	/* draw seconds ticks */
	ticks = ceil(g->schart_data.time_end - g->schart_data.time_start);
	if (ticks > 2) {
		int tick_margin;
		double tick_offset;

		tick_margin = (widget->allocation.width - STATUS_BOX_WIDTH - TIMELINE_MARGIN_FROM_RIGHT - STATUS_BOX_WIDTH) / ticks;
		tick_offset = g->schart_data.time_start * tick_margin;

		for (i = 1; i <= tick_margin; i++) {
			cairo_move_to(cr, i * tick_margin + tick_offset + STATUS_BOX_WIDTH, TIMELINE_MARGIN_FROM_TOP);
			cairo_line_to(cr, i * tick_margin + tick_offset + STATUS_BOX_WIDTH, TIMELINE_MARGIN_FROM_TOP + TIMELINE_TICK_LEN);
			cairo_stroke(cr);
		}
	}
}

#if 0

#define PIXELHEIGHT 20

static void schart_precalculate_chart_data(GtkWidget *widget)
{
#define PIXELHEIGHT 20
}


static int x_pos_from_event(e)
{
        return x;
}

static int pre_calculate_d_data(void)
{
        int occurance;
        unsigned int min_density = UINT_MAX;
        unsigned int max_density = UINT_MIN;
        unsigned int different_x_pixel_positions = 0;

        occurance = 1;

        for (c) {
                for (e) {
                        for (time) {
                                x_curr = x_pos_from_event(e);

                                if (x_curr != x_saved) {
                                       min_density = min(min_density,
occurance);
                                       max_density = max(max_density,
occurance); 
                                       occurance = 1;
                                       different_x_pixel_positions++;
                                }
                                x_saved = x_curr;
                                occurance++;
                        }
                }
        }

        /* catch the case where only one event was captured */
        if (min_density == UINT_MAX || max_density == UINT_MIN)
                min_density = max_density = 1;


        for (c) {
                for (e) {
                        for (time) {
                                x_curr = x_pos_from_event(e);

                                if (x_curr != x_saved) {
                                        /* lets go */
                                        x_pos = x_saxed;
                                        height = ((occurance - min_density) / max_density - min_density) * PIXELHEIGHT

                                        occurance = 1;
                                }
                                x_saved = x_curr;
                                occurance++;
                        }
                }
        }
}

#endif


static void draw(GtkWidget *widget, cairo_t *cr)
{
	Schart *g = (Schart *)(widget);

	/* disable antialiasing per default */
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);

	draw_background(widget, cr);

	if (g->schart_data_raw_list) {

		draw_timeline(widget, cr);

		draw_charts_background(widget, cr);

		draw_cpu_pictograms(widget, cr);

		switch (g->display_mode) {
		case DISPLAY_MODE_PEAKS:
			draw_charts(widget, cr);
			break;
		case DISPLAY_MODE_CURVES:
			draw_curves(widget, cr);
			break;
		default:
			abort();
			break;
		}
	}


	if (g->pointer_x != -1) {
		guint offset, no_cpus;

		no_cpus = g_slist_length(g->schart_data_raw_list);
		offset = (CHART_HEIGHT + CHART_MARGIN) * no_cpus + TIMELINE_MARGIN_FROM_TOP + 10;

		cairo_set_line_width(cr, 1);
		cairo_set_source_rgb (cr, .0, .0, .0);
		cairo_move_to (cr, g->pointer_x, TIMELINE_MARGIN_FROM_TOP);
		cairo_line_to (cr, g->pointer_x, offset);
		cairo_stroke(cr);
	}
}


static gboolean schart_expose(GtkWidget *widget, GdkEventExpose *event)
{
	cairo_t *cr;
	Schart *g;


	g = (Schart *)widget;

	if (g->schart_data.time_start < 0.0) {
		/* no data at the moment */
		return TRUE;
	}

	if (g->width == -1) {
		g->width = event->area.width;
		g->height = event->area.height;
	}

	/* get a cairo_t */
	cr = gdk_cairo_create(widget->window);

	cairo_rectangle(cr, event->area.x, event->area.y, event->area.width, event->area.height);
	cairo_clip (cr);

	draw(widget, cr);

	cairo_destroy (cr);

	//fprintf(stderr, "- %d - %d\n", g->width , g->height);
	gtk_widget_set_size_request(widget, g->width, g->height);

	return TRUE;
}



static gboolean configure_event(GtkWidget *widget, GdkEventMotion *event, Schart *schart_context)
{
	(void) widget;
	(void) event;
	(void) schart_context;

	fprintf(stderr, "configure_event\n");

#if 0
	schart_context->width = widget->area.width;
	schart_context->height = widget->area.height;

	gtk_widget_set_size_request(widget, g->width, g->height);
#endif

	return TRUE;
}


static gboolean motion_notify_event(GtkWidget *widget, GdkEventMotion *event, Schart *data)
{
	GdkModifierType state;
	Schart *g;
	gint x, y;

	(void) data;

	if (event->is_hint)
		gdk_window_get_pointer(event->window, &x, &y, &state);
	else {
		x = event->x;
		y = event->y;
	}

	//fprintf(stderr, "motion_notify_event: x: %d, y: %d\n", x, y);

	g = (Schart *)(widget);

	g->pointer_x = x;
	g->pointer_y = y;

	gtk_widget_queue_draw(widget);

	if (g->pointer_change_cb)
		g->pointer_change_cb(g->pointer_change_data, pixel_to_second(widget, x));

	return FALSE;
}

static gboolean button_press_event(GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
	//fprintf(stderr, "button_press_event\n");
	(void) widget;
	(void) event;
	(void) data;

	return TRUE;
}

static gboolean button_release_event(GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
	//fprintf(stderr, "button_release_event\n");
	(void) widget;
	(void) event;
	(void) data;

	return TRUE;
}

static gboolean leave_notify_event(GtkWidget *widget, GdkEventCrossing *event, gpointer data)
{
	Schart *g = (Schart *)(widget);

	(void) event;
	(void) data;

	g->pointer_x = -1;
	g->pointer_y = -1;

	if (g->pointer_change_cb)
		g->pointer_change_cb(g->pointer_change_data, -1.0);

	gtk_widget_queue_draw(widget);

	return FALSE;
}

static gboolean destroy_event(GtkWidget *widget, gpointer data)
{
	fprintf(stderr, "destroy_event\n");

	(void) widget;
	(void) data;

	return TRUE;
}

static void schart_data_init(Schart *g)
{
	g->schart_data.time_start = 0.0;
	g->schart_data.time_end   = 0.0;

	g->schart_data.charts_no = 0;
}


void schart_set_data_time_start(GtkWidget *w, double t)
{
	Schart *g = (Schart *)w;
	g->schart_data.time_start = t;
}


void schart_set_data_time_end(GtkWidget *w, double t)
{
	Schart *g = (Schart *)w;
	g->schart_data.time_end = t;
}


void schart_set_data_time_start_end(GtkWidget *w, double start, double end)
{
	Schart *g = (Schart *)w;
	g->schart_data.time_start = start;
	g->schart_data.time_end   = end;
}

void schart_set_data_charts_no(GtkWidget *w, unsigned charts_no)
{
	Schart *g = (Schart *)w;
	g->schart_data.charts_no = charts_no;
}


void schart_register_pointer_change_cb(GtkWidget *w, void (*fn)(GtkWidget *, double), GtkWidget *label)
{
	Schart *g = (Schart *)w;
	g->pointer_change_cb   = fn;
	g->pointer_change_data = label;
}

void schart_set_raw_data(GtkWidget *w, GSList *l)
{
	Schart *g = (Schart *)w;
	g->schart_data_raw_list = l;

	return;
}

void schart_display_mode(GtkWidget *w, int mode)
{
	Schart *g = (Schart *)w;

	if (mode >= DISPLAY_MODE_MAX) {
		fprintf(stderr, "Not allowed\n");
		return;
	}

	g->display_mode = mode;

	gtk_widget_queue_draw(w);
}

#define ZOOM_MULTIPLICATOR 1.5

void schart_zoom_in(GtkWidget *w)
{
	Schart *g = (Schart *)w;
	g->zoom_factor *= ZOOM_MULTIPLICATOR;
	g->width *= ZOOM_MULTIPLICATOR;

	gtk_widget_queue_draw(w);
}

void schart_zoom_out(GtkWidget *w)
{
	Schart *g = (Schart *)w;
	g->zoom_factor /= ZOOM_MULTIPLICATOR;
	g->width /= ZOOM_MULTIPLICATOR;

	gtk_widget_queue_draw(w);
}



GtkWidget *schart_new(void)
{
	Schart *g;
	GtkWidget *w = g_object_new(TYPE_SCHART, NULL);

	g = (Schart *) w;

	g_signal_connect(GTK_OBJECT(w), "button_press_event",
			    G_CALLBACK(button_press_event), NULL);
	g_signal_connect(GTK_OBJECT(w), "configure_event",
			   G_CALLBACK(configure_event), g);
	g_signal_connect(GTK_OBJECT(w), "motion_notify_event",
			   G_CALLBACK(motion_notify_event), g);
	g_signal_connect(GTK_OBJECT(w), "button_press_event",
			   G_CALLBACK(button_press_event), NULL);
	g_signal_connect(GTK_OBJECT(w), "button_release_event",
			   G_CALLBACK(button_release_event), NULL);
	g_signal_connect(GTK_OBJECT(w), "leave-notify-event",
			   G_CALLBACK(leave_notify_event), NULL);
	g_signal_connect(GTK_OBJECT(w), "destroy",
			   G_CALLBACK(destroy_event), NULL);

	gtk_widget_set_events(w, GDK_EXPOSURE_MASK
			      | GDK_LEAVE_NOTIFY_MASK
			      | GDK_BUTTON_PRESS_MASK
			      | GDK_BUTTON_RELEASE_MASK
			      | GDK_POINTER_MOTION_MASK
			      | GDK_POINTER_MOTION_HINT_MASK
			      | GDK_LEAVE_NOTIFY_MASK);


	/* init Graph */
	g->width = g->height = -1;
	g->zoom_factor = ZOOM_FACTOR_DEFAULT;
	g->display_mode = DISPLAY_MODE_PEAKS;

	g->pointer_change_cb    = NULL;
	g->schart_data_raw_list = NULL;

	schart_data_init(g);


	return w;
}


/*********************************************************************************
 *
 *   GTK Stuff
 */





/*
 * No matter what exit path is taken,
 * this function must called for sure
 */
static void main_quit(void)
{
	pr_err("close perf user database\n");

	pr_err("exiting perf-studio, bye\n");
}


/* this exit path must be called if we are GTK context,
 * this will shutdown the GTK subsystem */
static gboolean screen_quit(GtkWidget *widget, GtkWidget *event, gpointer data)
{
	(void) widget;
	(void) event;
	(void) data;

	main_quit();

	gtk_main_quit();

	return TRUE;
}






static void screen_err_msg(const gchar *format, ...)
{
	va_list  args;
	gchar *str;
	GtkWidget *dialog;

	va_start(args, format);
	str = g_strdup_vprintf (format, args);
	va_end(args);

	dialog = gtk_message_dialog_new(GTK_WINDOW(sc.screen.main_window), 0,
			                GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", str);
	gtk_dialog_run(GTK_DIALOG(dialog));
	g_free(str);
	gtk_widget_destroy(dialog);
}




static void perf_gtk_signal(int sig)
{
	psignal(sig, "perf");
	pr_err("signal quit perf studio\n");
	gtk_main_quit();
}


static void init_sighandler(void)
{
	signal(SIGSEGV, perf_gtk_signal);
	signal(SIGFPE,  perf_gtk_signal);
	signal(SIGINT,  perf_gtk_signal);
	signal(SIGQUIT, perf_gtk_signal);
	signal(SIGTERM, perf_gtk_signal);
}

static void perf_gtk_resize_main_window(GtkWidget *window)
{
	GdkRectangle rect;
	GdkScreen *screen;
	int monitor;
	int height;
	int width;

	screen = gtk_widget_get_screen(window);

	monitor = gdk_screen_get_monitor_at_window(screen, window->window);

	gdk_screen_get_monitor_geometry(screen, monitor, &rect);

	width	= rect.width * 3 / 4;
	height	= rect.height * 3 / 4;

	gtk_window_resize(GTK_WINDOW(window), width, height);
}


static void screen_set_theme_dark(GtkWidget *w)
{
	GdkColor c1, c2, c3;

	gdk_color_parse("#222222", &c1);
	gdk_color_parse("#cccccc", &c2);
	gdk_color_parse("#ff0000", &c3);

	gtk_widget_modify_base(w, GTK_STATE_NORMAL, &c3);
	gtk_widget_modify_base(w, GTK_STATE_ACTIVE, &c3);
	gtk_widget_modify_base(w, GTK_STATE_SELECTED, &c3);
	gtk_widget_modify_base(w, GTK_STATE_INSENSITIVE, &c3);
	gtk_widget_modify_base(w, GTK_STATE_PRELIGHT, &c3);

	gtk_widget_modify_bg(w, GTK_STATE_NORMAL, &c1);
	gtk_widget_modify_fg(w, GTK_STATE_NORMAL, &c2);
	gtk_widget_modify_text(w, GTK_STATE_NORMAL, &c3);

	gtk_widget_modify_bg(w, GTK_STATE_ACTIVE, &c1);
	gtk_widget_modify_fg(w, GTK_STATE_ACTIVE, &c2);
	gtk_widget_modify_text(w, GTK_STATE_ACTIVE, &c3);

	gtk_widget_modify_bg(w, GTK_STATE_SELECTED, &c1);
	gtk_widget_modify_fg(w, GTK_STATE_SELECTED, &c2);
	gtk_widget_modify_text(w, GTK_STATE_SELECTED, &c3);

	gtk_widget_modify_bg(w, GTK_STATE_INSENSITIVE, &c1);
	gtk_widget_modify_fg(w, GTK_STATE_INSENSITIVE, &c2);
	gtk_widget_modify_text(w, GTK_STATE_INSENSITIVE, &c3);

	gtk_widget_modify_bg(w, GTK_STATE_PRELIGHT, &c1);
	gtk_widget_modify_fg(w, GTK_STATE_PRELIGHT, &c2);
	gtk_widget_modify_text(w, GTK_STATE_PRELIGHT, &c3);


}


static void screen_apply_theme_to_widget(GtkWidget *w)
{
	switch (sc.screen.theme) {
		case THEME_COLOR_DARK:
			screen_set_theme_dark(w);
			break;
		default:
			break;
	}
}

static void register_private_signals(void)
{
	g_signal_new("foo", G_TYPE_OBJECT, G_SIGNAL_RUN_FIRST,
		     0, NULL, NULL, g_cclosure_marshal_VOID__POINTER,
		     G_TYPE_NONE, 1, G_TYPE_POINTER);
}



#define	PERF_STUDIO_WINDOW_TITLE "Perf Studio"


static void screen_init(int ac, const char **av)
{
	GtkSettings *settings;

	g_thread_init(NULL);
	gdk_threads_init();

	gtk_init(&ac, (char ***)&av);

	register_private_signals();

	settings = gtk_settings_get_default();
	gtk_rc_parse("theme.rc");
	gtk_rc_reset_styles(settings);

	sc.screen.main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	screen_apply_theme_to_widget(sc.screen.main_window);
	gtk_window_set_title(GTK_WINDOW(sc.screen.main_window), PERF_STUDIO_WINDOW_TITLE);

	/* if the user quits the app */
	g_signal_connect(G_OBJECT(sc.screen.main_window), "destroy",G_CALLBACK(screen_quit), NULL);
	g_signal_connect(G_OBJECT(sc.screen.main_window), "delete_event", G_CALLBACK(screen_quit), NULL);

	gtk_container_set_border_width(GTK_CONTAINER(sc.screen.main_window), 0);
}



static void screen_basic_layout_init(void)
{
	sc.screen.vbox = gtk_vbox_new(FALSE, 0);
	screen_apply_theme_to_widget(sc.screen.vbox);
	gtk_container_add(GTK_CONTAINER(sc.screen.main_window), sc.screen.vbox);
}


static void screen_menu_init(void)
{

	GtkWidget *menubar;
	GtkWidget *filemenu;
	GtkWidget *projectmenu;
	GtkWidget *systemmenu;
	GtkWidget *viewmenu;
	GtkWidget *helpmenu;

	GtkWidget *file;
	GtkWidget *file_new;
	GtkWidget *file_open;
	GtkWidget *file_quit;
	GtkWidget *file_sep;

	GtkWidget *view;
	GtkWidget *view_report;
	GtkWidget *project_recent;
	GtkWidget *project_manage;

	GtkWidget *project;
	GtkWidget *project_report;

	GtkWidget *systemm;
	GtkWidget *system_report;

	GtkWidget *help;
	GtkWidget *help_report;


	sc.screen.accel_group = gtk_accel_group_new();
	gtk_window_add_accel_group(GTK_WINDOW(sc.screen.main_window), sc.screen.accel_group);

	menubar     = gtk_menu_bar_new();
	gtk_widget_set_name(menubar, "menubar");

	filemenu    = gtk_menu_new();
	projectmenu = gtk_menu_new();
	systemmenu  = gtk_menu_new();
	viewmenu    = gtk_menu_new();
	helpmenu    = gtk_menu_new();


	/* File submenues */
	file      = gtk_menu_item_new_with_mnemonic("_File");
	file_new  = gtk_image_menu_item_new_from_stock(GTK_STOCK_NEW, NULL);
	file_open = gtk_image_menu_item_new_from_stock(GTK_STOCK_OPEN, NULL);
	file_sep  = gtk_separator_menu_item_new();
	file_quit = gtk_image_menu_item_new_from_stock(GTK_STOCK_QUIT, sc.screen.accel_group);

	gtk_widget_add_accelerator(file_quit, "activate", sc.screen.accel_group, GDK_KEY_q, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

	gtk_menu_item_set_submenu(GTK_MENU_ITEM(file), filemenu);
	gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), file_new);
	gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), file_open);
	gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), file_sep);
	gtk_menu_shell_append(GTK_MENU_SHELL(filemenu), file_quit);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), file);


	/* Projects submenues */
	project         = gtk_menu_item_new_with_mnemonic("_Projects");
	project_report  = gtk_image_menu_item_new_from_stock(GTK_STOCK_NEW, NULL);
	project_recent  = gtk_menu_item_new_with_mnemonic("Re_cent Projects");
	project_manage  = gtk_menu_item_new_with_mnemonic("_Manage Projects");

	gtk_menu_item_set_submenu(GTK_MENU_ITEM(project), projectmenu);
	gtk_menu_shell_append(GTK_MENU_SHELL(projectmenu), project_report);
	gtk_menu_shell_append(GTK_MENU_SHELL(projectmenu), project_recent);
	gtk_menu_shell_append(GTK_MENU_SHELL(projectmenu), project_manage);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), project);


	/* System submenues */
	systemm         = gtk_menu_item_new_with_mnemonic("_Systems");
	system_report  = gtk_image_menu_item_new_from_stock(GTK_STOCK_NEW, NULL);

	gtk_menu_item_set_submenu(GTK_MENU_ITEM(systemm), systemmenu);
	gtk_menu_shell_append(GTK_MENU_SHELL(systemmenu), system_report);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), systemm);


	/* View submenues */
	view         = gtk_menu_item_new_with_mnemonic("_View");
	view_report  = gtk_image_menu_item_new_from_stock(GTK_STOCK_NEW, NULL);

	gtk_menu_item_set_submenu(GTK_MENU_ITEM(view), viewmenu);
	gtk_menu_shell_append(GTK_MENU_SHELL(viewmenu), view_report);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), view);


	/* Help submenues */
	help        = gtk_menu_item_new_with_mnemonic("_Help");
	help_report  = gtk_image_menu_item_new_from_stock(GTK_STOCK_NEW, NULL);

	gtk_menu_item_set_submenu(GTK_MENU_ITEM(help), helpmenu);
	gtk_menu_shell_append(GTK_MENU_SHELL(helpmenu), help_report);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), help);



	gtk_box_pack_start(GTK_BOX(sc.screen.vbox), menubar, FALSE, FALSE, 0);


	g_signal_connect(G_OBJECT(file_quit), "activate", G_CALLBACK(screen_quit), NULL);
	g_signal_connect_swapped(G_OBJECT(sc.screen.main_window), "destroy", G_CALLBACK(screen_quit), NULL);

	gtk_widget_show_all(sc.screen.vbox);
	gtk_widget_set_name(sc.screen.vbox, "menubar");
	screen_apply_theme_to_widget(menubar);

	return;
}


static void screen_modern_logo(void)
{
	GtkStyle *mstyle;
	GtkWidget *event_box;
	char image_path[256];

	GdkPixbuf *pixbuf;
	GdkBitmap *bitmap=NULL;
	GdkPixmap *pixmap=NULL;
	GError *gerror = NULL;

	event_box = gtk_event_box_new();
	gtk_event_box_set_visible_window( GTK_EVENT_BOX(event_box), TRUE );
	gtk_widget_show(event_box);

	gtk_widget_set_usize(event_box, 50, 50);

	snprintf(image_path, sizeof(image_path), "%s%s", sc.pixmapdir, "back2.png");
	pixbuf = gdk_pixbuf_new_from_file(image_path, &gerror);
	gdk_pixbuf_render_pixmap_and_mask(pixbuf, &pixmap, &bitmap, 0);
	gtk_widget_set_style(GTK_WIDGET(event_box), NULL);

	mstyle = gtk_style_new();
	gtk_style_ref(mstyle);
	mstyle->bg_pixmap[GTK_STATE_NORMAL] = pixmap;
	gtk_widget_set_style(event_box, mstyle);
	gtk_style_unref(mstyle);

	gtk_box_pack_start(GTK_BOX(sc.screen.vbox), event_box, FALSE, TRUE, 0);
	gtk_widget_show_all(event_box);
}


static void screen_statusbar_init(void)
{
	const gchar *info;
	guint id;

	sc.screen.statusbar = gtk_statusbar_new();


	/* stack for info messages */
	g_object_set_data(G_OBJECT(sc.screen.statusbar), "info", (gpointer)
			"1");
	g_object_set_data(G_OBJECT(sc.screen.statusbar), "info",
			(gpointer) "2");
	g_object_set_data(G_OBJECT(sc.screen.statusbar), "info",
			(gpointer) "3");

	/* stack for warning messages */
	g_object_set_data(G_OBJECT(sc.screen.statusbar), "warning",
			(gpointer) "A");
	g_object_set_data(G_OBJECT(sc.screen.statusbar), "warning",
			(gpointer) "B");
	g_object_set_data(G_OBJECT(sc.screen.statusbar),
			"warning", (gpointer) "C");

	/* get id for the message at the top of the
	 *      * info stack? */
	id = gtk_statusbar_get_context_id(GTK_STATUSBAR(sc.screen.statusbar), "info");

	/* show the top message from the info stack
	 *      * ? */
	info = "Startup Sequence passed";
	gtk_statusbar_push(GTK_STATUSBAR(sc.screen.statusbar), id, info);


	gtk_box_pack_start(GTK_BOX(sc.screen.vbox), sc.screen.statusbar, FALSE, FALSE, 0);

	gtk_widget_show_all(sc.screen.statusbar);

}


static struct background_process_worker *background_process_worker_new(void)
{
	struct background_process_worker *bpw;

	bpw = malloc(sizeof(*bpw));
	if (!bpw) {
		pr_err("out of mem");
		return NULL;
	}

	memset(bpw, 0, sizeof(*bpw));
	bpw->state = NO_STATE;

	return bpw;
}


static void background_process_worker_delete(struct background_process_worker *w)
{
	free(w);
}

static gboolean perf_run_analysis_end(GtkWidget *widget, gpointer data)
{
	(void) widget;
	(void) data;

	gtk_widget_destroy(sc.screen.perf_run_window);

	return true;
}


static void process_executer_monitor(GtkWidget *w, void *foo, void *priv_data)
{
	struct background_process_worker *bpw = priv_data;

	(void) w;
	(void) foo;

	switch (bpw->state) {
	case BACKGROUND_PROCESS_STATE_INITIALIZE:
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(bpw->progress_bar), 0.01);
		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(bpw->progress_bar), "Initialize");
		break;
	case BACKGROUND_PROCESS_STATE_START:
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(bpw->progress_bar), 0.2);
		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(bpw->progress_bar), "Execute program");
		break;
	case BACKGROUND_PROCESS_STATE_FINISHED:
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(bpw->progress_bar), 0.85);
		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(bpw->progress_bar), "Finished, join thread");
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(bpw->progress_bar), 0.99);
		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(bpw->progress_bar), "Finished");

		/* before we delete the data structure and give control to
		 * the main pannel we reprogram the button and make the
		 * button sensitive again
		 */
		g_signal_handler_disconnect(G_OBJECT(bpw->control_button), bpw->control_button_id);
		g_signal_connect(G_OBJECT(bpw->control_button), "clicked", G_CALLBACK(perf_run_analysis_end), bpw);
		gtk_button_set_label(GTK_BUTTON(bpw->control_button), "OK");
		gtk_widget_set_sensitive(bpw->control_button, true);

		background_process_worker_delete(bpw);

		break;
	default:
		assert(0);
	}

}

static void execute_workload(void)
{
	GError *gerror = NULL;
	int exit_status = 0;
	//gchar *argv[] = { (char *)"/usr/bin/urxvt", (char *)"-e", (char *)"sh", (char *)"-c", (char *)"ls -R /", NULL };
	gchar *argv[] = { (char *)"/usr/bin/gnome-open", (char *)"file:///", NULL };

	if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_FILE_AND_ARGV_ZERO, NULL, NULL,
		          NULL, NULL, &exit_status, &gerror)) {
		pr_err("exec error: %s\n", gerror->message);
	}
}


static void *argument_thread(void *args)
{
	int i;
	struct background_process_worker *bpw = args;

	g_signal_connect(bpw->progress_bar, "foo", G_CALLBACK(process_executer_monitor), bpw);

	bpw->state = BACKGROUND_PROCESS_STATE_INITIALIZE;


	gdk_threads_enter();
	gtk_signal_emit_by_name(GTK_OBJECT(bpw->progress_bar), "foo");
	gdk_threads_leave();

	for (i = 0; i < 2; i++) {

		/* sleep a while */
		sleep(rand() % 3);

		bpw->state = BACKGROUND_PROCESS_STATE_START;

		execute_workload();

		/* get gtk thread lock */
		gdk_threads_enter();

		gtk_signal_emit_by_name(GTK_OBJECT(bpw->progress_bar), "foo");

		/* Make sure all X commands are sent to the X server; not strictly
		 * necessary here, but always a good idea when you do anything
		 * from a thread other than the one where the main loop is running.
		 */
		gdk_flush();

		/* release GTK thread lock */
		gdk_threads_leave();
	}


	bpw->state = BACKGROUND_PROCESS_STATE_FINISHED;
	gdk_threads_enter();
	gtk_signal_emit_by_name(GTK_OBJECT(bpw->progress_bar), "foo");
	gdk_threads_leave();

	g_thread_exit(0);

	return NULL;
}


static gboolean perf_run_analysis_clicked(GtkWidget *widget, gpointer data)
{
	struct background_process_worker *bpw;
	GError *gerror = NULL;

	(void) widget;

	bpw = data;

	/* widget is the button which started us
	 * we now disable this button
	 */

	gtk_widget_set_sensitive(widget, false);

	if (bpw->state != NO_STATE) {
		/* user tries to click to often
		 * and to spawn several instances */
		return TRUE;
	}

	bpw->state = BACKGROUND_PROCESS_PREPARE_TO_CREATE;

	bpw->gthread = g_thread_create(argument_thread, bpw, FALSE, &gerror);
	if (!bpw->gthread)
	{
		pr_err("Failed to create YES thread: %s\n", gerror->message);
		return FALSE;
	}

	return TRUE;
}


static void perf_run_dialog_window(GtkToolButton *toolbutton, gpointer user_data)
{
	GtkWidget *frame;
	GtkWidget *quitbox;
	struct background_process_worker *bpw;
	GtkWidget *view;
	GtkTextBuffer *buffer;
	GtkWidget *vbox;

	(void) toolbutton;
	(void) user_data;

	bpw = background_process_worker_new();
	if (!bpw)
		return;

	sc.screen.perf_run_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	/* if the user quits the app */
	g_signal_connect(G_OBJECT(sc.screen.main_window), "destroy",G_CALLBACK(screen_quit), NULL);
	g_signal_connect(G_OBJECT(sc.screen.main_window), "delete_event", G_CALLBACK(screen_quit), NULL);

	gtk_container_set_border_width(GTK_CONTAINER(sc.screen.perf_run_window), 0);
	gtk_window_set_modal((GtkWindow *)sc.screen.perf_run_window, TRUE);

	vbox = gtk_vbox_new(FALSE, 0);

	frame = gtk_frame_new("Run Analysis");
	gtk_container_set_border_width(GTK_CONTAINER (frame), 2);

	/* display some text */
	view = gtk_text_view_new ();
	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
	gtk_text_buffer_set_text(buffer, "Now I try to execute the programm and so \n"
			         "some initial analysis. Based on the results I will\n"
				 "give advices. For example: I check if the program\n"
				 "use threads and if so if there are perforamance\n"
				 "critical contention within the treads\n"
				 "---------------------------------------\n"
				 "This will run the programm exactly as specified. So be\n"
				 "aware that this can take some time", -1);
	gtk_container_add (GTK_CONTAINER (frame), view);


	gtk_widget_set_size_request(frame, 500, 200);
	gtk_widget_show_all(frame);

	gtk_box_pack_start(GTK_BOX(vbox), frame, TRUE, TRUE, 0);

	bpw->progress_bar = gtk_progress_bar_new();
	gtk_box_pack_start(GTK_BOX(vbox), bpw->progress_bar, FALSE, FALSE, 20);
	gtk_widget_show(bpw->progress_bar);

	quitbox = gtk_hbox_new(FALSE, 0);
	bpw->control_button = gtk_button_new_with_label("Start Analysis");
	bpw->control_button_id = g_signal_connect(G_OBJECT(bpw->control_button), "clicked", G_CALLBACK(perf_run_analysis_clicked), bpw);


	gtk_box_pack_end(GTK_BOX(quitbox), bpw->control_button, FALSE, FALSE, 10);
	gtk_widget_show_all(quitbox);
	gtk_box_pack_start(GTK_BOX(vbox), quitbox, FALSE, FALSE, 5);

	gtk_widget_show_all(vbox);

	gtk_container_add(GTK_CONTAINER(sc.screen.perf_run_window), vbox);
	gtk_window_set_position((GtkWindow *)sc.screen.perf_run_window, GTK_WIN_POS_CENTER);
	gtk_window_present((GtkWindow *)sc.screen.perf_run_window);
	gtk_widget_show((GtkWidget *)sc.screen.perf_run_window);

	return;
}

#if 0
static void screen_toolbar_init(void)
{

	GtkWidget *toolbar;
	GtkToolItem *toolbar_new;
	GtkToolItem *toolbar_open;
	GtkToolItem *toolbar_save;
	GtkToolItem *toolbar_sep;
	GtkToolItem *toolbar_exit;

	toolbar = gtk_toolbar_new();
	gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_ICONS);

	gtk_container_set_border_width(GTK_CONTAINER(toolbar), 0);


	toolbar_new = gtk_tool_button_new_from_stock(GTK_STOCK_NEW);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolbar_new, -1);

	toolbar_open = gtk_tool_button_new_from_stock(GTK_STOCK_OPEN);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolbar_open, -1);
	g_signal_connect(G_OBJECT(toolbar_open), "clicked", G_CALLBACK(perf_run_dialog_window), NULL);

	toolbar_save = gtk_tool_button_new_from_stock(GTK_STOCK_SAVE);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolbar_save, -1);

	toolbar_sep = gtk_separator_tool_item_new();
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolbar_sep, -1);

	toolbar_exit = gtk_tool_button_new_from_stock(GTK_STOCK_QUIT);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), toolbar_exit, -1);

	gtk_box_pack_start(GTK_BOX(sc.screen.vbox), toolbar, FALSE, FALSE, 0);

	gtk_widget_show_all(sc.screen.vbox);

	return;
}
#endif





/* FIXME: add prefix for functions */
static gboolean zoom_in(gpointer data)
{
	GtkWidget *w = data;

	schart_zoom_in(w);

	return FALSE;
}

static gboolean zoom_out(gpointer data)
{
	GtkWidget *w = data;

	schart_zoom_out(w);

	return FALSE;
}

static gboolean display_mode_peaks(gpointer data)
{
	GtkWidget *w = data;

	schart_display_mode(w, DISPLAY_MODE_PEAKS);

	return FALSE;
}

static gboolean display_mode_curves(gpointer data)
{
	GtkWidget *w = data;

	schart_display_mode(w, DISPLAY_MODE_CURVES);

	return FALSE;
}

static void pointer_changed_callback(GtkWidget *label, double new_x)
{
	char time_repr[64];

	if (new_x < 0.0) {
		gtk_label_set_markup(GTK_LABEL(label), "             ");
		return;
	}

	g_snprintf(time_repr, sizeof(time_repr) - 1, " %.5lf sec    ", new_x);
	gtk_label_set_markup(GTK_LABEL(label), time_repr);
}


static GtkWidget *screen_nootbook_thread_analyzer_buttonbox_new(void)
{
	GtkWidget *hbox;
	GtkWidget *button;
	GtkWidget *image;
	GtkWidget *label;
	char image_path[PATH_MAX];

	assert(sc.screen.schart);

	hbox = gtk_hbox_new(FALSE, 0);

	button = gtk_button_new();
	snprintf(image_path, sizeof(image_path), "%s%s", sc.buttondir, "gtk-zoom-in.png");
	image = gtk_image_new_from_file(image_path);
	gtk_button_set_image ((GtkButton *) button, image);
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	g_signal_connect_swapped (G_OBJECT(button), "clicked",
			G_CALLBACK(zoom_in), sc.screen.schart);

	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);


	button = gtk_button_new();
	snprintf(image_path, sizeof(image_path), "%s%s", sc.buttondir, "gtk-zoom-out.png");
	image = gtk_image_new_from_file(image_path);
	gtk_button_set_image ((GtkButton *) button, image);
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	g_signal_connect_swapped(G_OBJECT(button), "clicked",
			G_CALLBACK(zoom_out), sc.screen.schart);

	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);


	button = gtk_button_new();
	snprintf(image_path, sizeof(image_path), "%s%s", sc.buttondir, "gnumeric2.png");
	image = gtk_image_new_from_file(image_path);
	gtk_button_set_image ((GtkButton *) button, image);
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	g_signal_connect_swapped (G_OBJECT(button), "clicked",
			G_CALLBACK(display_mode_peaks), sc.screen.schart);

	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);

	button = gtk_button_new();
	snprintf(image_path, sizeof(image_path), "%s%s", sc.buttondir, "lybniz.png");
	image = gtk_image_new_from_file(image_path);
	gtk_button_set_image ((GtkButton *) button, image);
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	g_signal_connect_swapped(G_OBJECT(button), "clicked",
			G_CALLBACK(display_mode_curves), sc.screen.schart);

	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);


	/* time section */

	label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label), " ");
	gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, FALSE, 0);

	label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label), "time:   ");
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label), "                  ");
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
	schart_register_pointer_change_cb(sc.screen.schart, pointer_changed_callback, label);

	label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label), " end:            ");
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label), "         ");
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label), " delta:           ");
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);


	gtk_widget_show_all(hbox);

	return hbox;
}

#if 0

#endif


static GtkWidget *screen_nootbook_thread_analyzer_left_new(void)
{
	GtkWidget *vbox;
	GtkWidget *scrollwin;
	GtkWidget *button_bar;


	sc.screen.schart = schart_new();

	vbox = gtk_vbox_new(FALSE, 0);

	scrollwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrollwin), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_show(scrollwin);

	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrollwin), sc.screen.schart);

	button_bar = screen_nootbook_thread_analyzer_buttonbox_new();
	gtk_box_pack_start(GTK_BOX(vbox), button_bar, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), scrollwin, TRUE, TRUE, 0);

	gtk_widget_show_all(vbox);

	return vbox;
}

static gboolean toggle_max(GtkWidget *widget, GtkWidget *event, gpointer data)
{
	(void) widget;
	(void) event;
	(void) data;
	//GtkPaned *paned = data;
	fprintf(stderr, "enter\n");
	//gtk_paned_set_position(paned, 65);

	return TRUE;
}

static gboolean toggle_min(GtkWidget *widget, GtkWidget *event, gpointer data)
{
	(void) widget;
	(void) event;
	(void) data;
	//GtkPaned *paned = data;
	fprintf(stderr, "leave\n");
	//gtk_paned_set_position(paned, 10);

	return TRUE;
}

static gboolean screen_nootbook_thread_analyzer_legend_draw_callback(GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
	cairo_t *cr;

	(void) data;

	/* get a cairo_t */
	cr = gdk_cairo_create(widget->window);
	gtk_widget_set_size_request(widget, 100, 100);

	cairo_rectangle(cr, event->area.x, event->area.y, event->area.width, event->area.height);
	cairo_clip (cr);

	cairo_set_line_width(cr, 3);
	cairo_set_source_rgb (cr, .0, .0, .0);
	cairo_move_to (cr, 5, 5);
	cairo_line_to (cr, 50, 0);
	cairo_stroke(cr);

	cairo_destroy (cr);

	//fprintf(stderr, "- %d - %d\n", g->width , g->height);

	return TRUE;
}


static GtkWidget *screen_nootbook_thread_analyzer_legend_new(void)
{
	GtkWidget *darea;

	darea = gtk_drawing_area_new();

	g_signal_connect(darea, "expose-event", G_CALLBACK(screen_nootbook_thread_analyzer_legend_draw_callback), NULL);

	return darea;
}


static GtkWidget *screen_nootbook_thread_analyzer_right_legend_new(void)
{
	GtkWidget *expander;
	GtkWidget *entry;
	GtkWidget *table;
	GdkColor c1, c2, c3;
	GtkWidget *colorbutton;

	gdk_color_parse("#ff4444", &c1);
	gdk_color_parse("#44ff44", &c2);
	gdk_color_parse("#4444ff", &c3);

	expander= gtk_expander_new("Event Color Legend");

	table = gtk_table_new(3, 2, TRUE);

	gtk_table_set_row_spacings(GTK_TABLE(table), 2);
	gtk_table_set_col_spacings(GTK_TABLE(table), 2);


	colorbutton = gtk_color_button_new_with_color(&c1);
	gtk_button_set_relief(GTK_BUTTON(colorbutton), GTK_RELIEF_NONE);
	gtk_table_attach_defaults(GTK_TABLE(table), colorbutton, 0, 1, 0, 1);

	entry = gtk_label_new("FUTEX");
	gtk_table_attach_defaults(GTK_TABLE(table), entry, 1, 2, 0, 1);



	colorbutton = gtk_color_button_new_with_color(&c2);
	gtk_button_set_relief(GTK_BUTTON(colorbutton), GTK_RELIEF_NONE);
	gtk_table_attach_defaults(GTK_TABLE(table), colorbutton, 0, 1, 1, 2);

	entry = gtk_label_new("MALLOC");
	gtk_table_attach_defaults(GTK_TABLE(table), entry, 1, 2, 1, 2);



	colorbutton = gtk_color_button_new_with_color(&c3);
	gtk_button_set_relief(GTK_BUTTON(colorbutton), GTK_RELIEF_NONE);
	gtk_table_attach_defaults(GTK_TABLE(table), colorbutton, 0, 1, 2, 3);

	entry = gtk_label_new("MMAP");
	gtk_table_attach_defaults(GTK_TABLE(table), entry, 1, 2, 2, 3);



	gtk_container_add(GTK_CONTAINER(expander), table);
	gtk_expander_set_expanded(GTK_EXPANDER (expander), TRUE);

	return expander;
}


static GtkWidget *screen_nootbook_thread_analyzer_right_new(GtkWidget *paned)
{
	GtkWidget *vbox;
	GtkWidget *frame;
	GtkWidget *legend;
	GtkWidget *eventbox;

	vbox = gtk_vbox_new(FALSE, 10);

	legend = screen_nootbook_thread_analyzer_right_legend_new();
	gtk_widget_show(legend);
	gtk_box_pack_start(GTK_BOX(vbox), legend, FALSE, FALSE, 0);

	frame = screen_nootbook_thread_analyzer_legend_new();
	gtk_widget_show(frame);
	gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 0);

	eventbox = gtk_event_box_new();
	gtk_container_add(GTK_CONTAINER(eventbox), vbox);
	g_signal_connect(eventbox, "enter-notify-event", G_CALLBACK(toggle_max), paned);
	g_signal_connect(eventbox, "leave-notify-event", G_CALLBACK(toggle_min), paned);


	return eventbox;
}

static GtkWidget *screen_nootbook_thread_analyzer_control(void)
{
	GtkWidget *hbox;
	GtkWidget *button;
	GtkWidget *image;
	GtkWidget *label;
	char image_path[PATH_MAX];

	hbox = gtk_hbox_new(FALSE, 0);

	label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label),
			"<span  size=\"x-large\" font_weight=\"normal\" foreground=\"#666\"> Futex Analyzer </span>");
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	button = gtk_button_new();
	/* also nice gnome-cpu-frequency-applet.png */
	snprintf(image_path, sizeof(image_path), "%s%s", sc.buttondir, "preferences-desktop.png");
	image = gtk_image_new_from_file(image_path);
	gtk_button_set_image(GTK_BUTTON(button), image);
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 5);

	button = gtk_button_new();
	/* also nice gnome-cpu-frequency-applet.png */
	snprintf(image_path, sizeof(image_path), "%s%s", sc.buttondir, "elisa.png");
	image = gtk_image_new_from_file(image_path);
	gtk_button_set_image(GTK_BUTTON(button), image);
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 5);

	button = gtk_button_new();
	/* also nice gnome-cpu-frequency-applet.png */
	snprintf(image_path, sizeof(image_path), "%s%s", sc.buttondir, "gnome-cpu-frequency-applet.png");
	image = gtk_image_new_from_file(image_path);
	gtk_button_set_image(GTK_BUTTON(button), image);
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 5);

	button = gtk_button_new();
	/* also nice gnome-cpu-frequency-applet.png */
	snprintf(image_path, sizeof(image_path), "%s%s", sc.buttondir, "help-contents2.png");
	image = gtk_image_new_from_file(image_path);
	gtk_button_set_image(GTK_BUTTON(button), image);
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 5);

	return  hbox;
}

void screen_nootbook_thread_analyzer_init(GtkWidget *notebook)
{
	GtkWidget *label;
	GtkWidget *left, *right;
	GtkWidget *paned;
	GtkWidget *vbox;
	GtkWidget *hbox;

	vbox = gtk_vbox_new(FALSE, 0);
	hbox = screen_nootbook_thread_analyzer_control();

	paned = gtk_hpaned_new();

	left = screen_nootbook_thread_analyzer_left_new();
	gtk_paned_pack1(GTK_PANED(paned), left, TRUE, FALSE);

	right = screen_nootbook_thread_analyzer_right_new(paned);
	gtk_paned_pack2(GTK_PANED(paned), right, FALSE, TRUE);

	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), paned, TRUE, TRUE, 0);

	label = gtk_label_new("Thread Analyzer");
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox, label);
}





static void control_window_reload_new_project(struct studio_context *xsc, gchar *path)
{
	assert(sc.screen.project_overview);
	assert(sc.screen.control_pane);

	/* first, we destroy the old widget */
	gtk_widget_destroy(sc.screen.project_overview);

	sc.screen.project_overview = screen_mainwindow_control_overview_new(xsc, path);
	gtk_paned_pack2(GTK_PANED(sc.screen.control_pane), sc.screen.project_overview, FALSE, FALSE);
	gtk_widget_show_all(sc.screen.control_pane);
}



static GtkWidget *screen_mainwindow_control_new(void)
{
	GtkWidget *left_selection;

	sc.screen.control_pane = gtk_hpaned_new();

	left_selection = control_pane_new(&sc);
	gtk_paned_pack1(GTK_PANED(sc.screen.control_pane), left_selection, FALSE, FALSE);

	sc.screen.project_overview = screen_mainwindow_control_overview_new(&sc, NULL);
	gtk_paned_pack2(GTK_PANED(sc.screen.control_pane), sc.screen.project_overview, FALSE, FALSE);


	return sc.screen.control_pane;
}


static void screen_toolbar_mainwindow_init(void)
{
	sc.screen.main_paned = gtk_vpaned_new();

	/* upper side control panel */
	sc.screen.main_paned_control = screen_mainwindow_control_new();
	gtk_paned_pack1(GTK_PANED(sc.screen.main_paned), sc.screen.main_paned_control, TRUE, FALSE);

	/* down side workspace (notebook) panel */
	sc.screen.main_paned_workspace = screen_notebook_main_init(&sc);
	gtk_paned_pack2(GTK_PANED(sc.screen.main_paned), sc.screen.main_paned_workspace, TRUE, TRUE);

	gtk_box_pack_start(GTK_BOX(sc.screen.vbox), sc.screen.main_paned, TRUE, TRUE, 0);
	gtk_widget_show_all(sc.screen.main_paned);
}


static void screen_new_project_journey_cb(struct studio_assitant_new_project_data *pd)
{
	assert(pd);
	db_local_generate_project_file(pd);
	studio_assitant_new_project_data_free(pd);
}


static void screen_new_project_journey(char *project_name)
{
	assert(project_name);

	assistant_new(screen_new_project_journey_cb, project_name);
#if 0
	bool ret;
	struct db_global_project_conf_entry db_entry;

	memset(&db_entry, 0, sizeof(db_entry));

	snprintf(db_entry.name, DB_PROJECT_NAME_MAX, "%s", project_name);
	snprintf(db_entry.db_path, PATH_MAX - 1, "%s", "/fooo");

	/*
	 * Great: all data available and checked!
	 * Now create the local configuration file
	 * and add entry in global configuration
	 * file
	 */

	ret = db_global_add_new_project("foo");
	if (!ret) {
		screen_err_msg("Cannot add new perf session to global database");
	}
#endif

}

static gboolean screen_intro_dialog_new_project_quick(GtkWidget *entry)
{
	(void) entry;

	gtk_widget_destroy(sc.screen.dialog_window);

	return TRUE;
}

static gboolean screen_intro_dialog_new_project_sig(GtkWidget *entry)
{
	const gchar *text;
	char *project_name;

	text = gtk_entry_get_text(GTK_ENTRY(entry));
	if (!text || (strlen(text) < 1)) {
		screen_err_msg("No project name given!");
		gtk_widget_grab_focus(sc.screen.dialog_window);
		return true;
	}

	if ((strlen(text) > DB_PROJECT_NAME_MAX)) {
		screen_err_msg("Project name to large!");
		gtk_widget_grab_focus(sc.screen.dialog_window);
		return true;
	}

	pr_debug("create new project \"%s\"\n", text);


	/*
	 * Fine, project name is unique, start
	 * to get important initial data and start
	 * a small journey. Ask for
	 *
	 * o Full path of executable
	 * o All arguments
	 * o Working directory (present the current dir as starting point)
	 */

	/* because we destroy the superclass dialog window we make
	 * a copy of the project name, we later must free the string */
	project_name = strdup(text);
	if (!project_name) {
		screen_err_msg("Unable to allocate memory");
		screen_quit(NULL, NULL, NULL);
	}

	gtk_widget_destroy(sc.screen.dialog_window);
	screen_new_project_journey(project_name);

	return TRUE;
}

#define INTRO_FRAME_BORDER 5

static void screen_intro_dialog_new_project(GtkWidget *w)
{
	GtkWidget *frame;
	GtkWidget *table;
	GtkWidget *label1;
	GtkWidget *entry;
	GtkWidget *button;


	table = gtk_table_new(1, 3, FALSE);
	gtk_table_set_row_spacings(GTK_TABLE(table), 5);
	gtk_table_set_col_spacings(GTK_TABLE(table), 5);

	frame = gtk_frame_new("Create New Project");
	gtk_container_add(GTK_CONTAINER(frame), table);

	label1 = gtk_label_new("Project Name");
	gtk_table_attach(GTK_TABLE(table), label1, 0, 1, 0, 1,
			 GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK,
			 5, 5);

	entry = gtk_entry_new();
	gtk_table_attach(GTK_TABLE(table), entry, 1, 2, 0, 1,
			 GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 5, 5);
	button = gtk_button_new_with_label ("Create Project");


	g_signal_connect_swapped(GTK_OBJECT(button),
			         "clicked",
			         G_CALLBACK(screen_intro_dialog_new_project_sig),
				 entry);

	gtk_table_attach(GTK_TABLE(table), button, 2, 3, 0, 1,
			 GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 5, 5);

	gtk_widget_show(table);
	gtk_widget_show(label1);
	gtk_widget_show(entry);
	gtk_widget_show(button);

	gtk_container_set_border_width(GTK_CONTAINER (frame), INTRO_FRAME_BORDER);
	gtk_widget_show(frame);
	gtk_box_pack_start(GTK_BOX(w), frame, TRUE, TRUE, 0);
}


static void screen_intro_dialog_quick_session(GtkWidget *w)
{
	GtkWidget *frame;
	GtkWidget *table;
	GtkWidget *label1;
	GtkWidget *entry;
	GtkWidget *button;



	table = gtk_table_new(1, 3, FALSE);
	frame = gtk_frame_new("Quick Session");
	gtk_container_add(GTK_CONTAINER(frame), table);

	label1 = gtk_label_new("Project Name");
	gtk_table_attach(GTK_TABLE(table), label1, 0, 1, 0, 1,
			 GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK,
			 5, 5);

	entry = gtk_entry_new();
	gtk_table_attach(GTK_TABLE(table), entry, 1, 2, 0, 1,
			 GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 5, 5);
	button = gtk_button_new_with_label ("Create Project");


	g_signal_connect_swapped(GTK_OBJECT(button),
			         "clicked",
				 G_CALLBACK(screen_intro_dialog_new_project_quick),
				 entry);

	gtk_table_attach(GTK_TABLE(table), button, 2, 3, 0, 1,
			 GTK_FILL | GTK_SHRINK, GTK_FILL | GTK_SHRINK, 5, 5);

	gtk_widget_show(table);
	gtk_widget_show(label1);
	gtk_widget_show(entry);
	gtk_widget_show(button);

	gtk_container_set_border_width(GTK_CONTAINER(frame), 5);
	gtk_widget_show(frame);
	gtk_box_pack_start(GTK_BOX(w), frame, TRUE, TRUE, 0);
}

static void screen_intro_dialog_existing_activated(GtkTreeView *view,
		GtkTreePath *path, GtkTreeViewColumn *col, gpointer user_data)
{
	GtkTreeIter iter;
	GtkTreeModel *model;
	struct studio_context *xsc;

	assert(user_data);
	(void) col;

	xsc = user_data;

	model = gtk_tree_view_get_model(view);

	if (gtk_tree_model_get_iter(model, &iter, path)) {
		gchar *name, *project_path;
		gtk_tree_model_get(model, &iter, 0, &name, -1);
		gtk_tree_model_get(model, &iter, 1, &project_path, -1);
		gtk_widget_destroy(sc.screen.dialog_window);
		sc.screen.dialog_window = NULL;
		g_print("project selected: %s, path: %s\n", name, project_path);
		control_window_reload_new_project(xsc, project_path);
		g_free(name);
		g_free(project_path);
	}
}



static void screen_intro_dialog_existing_project(struct studio_context *xsc, GtkWidget *w)
{
	GtkWidget *frame;
	GtkListStore *lista1;
	GtkWidget *tree1;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeIter iter;
	GSList *gslist;
	struct db_projects_summary *ps;

	assert(xsc);

	db_generic_get_projects_summaries(&ps);
	if (!ps) {
		/* no database entry available */
		return;
	}


	lista1 = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	tree1 = gtk_tree_view_new_with_model(GTK_TREE_MODEL(lista1));
	gtk_widget_show(tree1);

	g_signal_connect(tree1,
			 "row-activated",
			 G_CALLBACK(screen_intro_dialog_existing_activated),
			 xsc);


	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes("Name", renderer, "text", 0,NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree1), column);
	column = gtk_tree_view_column_new_with_attributes("Path", renderer, "text" ,1,NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree1), column);
	column = gtk_tree_view_column_new_with_attributes("Last Used", renderer, "text",2,NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree1), column);

	gslist = ps->list;
	while (gslist != NULL) {
		struct db_project_summary *pss;
		gchar *delta;

		pss = gslist->data;

		delta = studio_utils_human_time_diff(pss->last_accessed);
		assert(delta);

		gtk_list_store_append(lista1, &iter);
		gtk_list_store_set(lista1, &iter, 0, pss->name,
						  1, pss->path,
						  2, delta,
						  -1);

		g_free(delta);
		gslist = g_slist_next(gslist);
	}

	frame = gtk_frame_new("Recent Projects");
	gtk_container_add(GTK_CONTAINER(frame), tree1);

	gtk_widget_show_all(frame);
	gtk_container_set_border_width(GTK_CONTAINER(frame), INTRO_FRAME_BORDER);
	gtk_box_pack_start(GTK_BOX(w), frame, TRUE, TRUE, 0);

	/* projects not needed anymore */
	db_generic_get_projects_summary_free(ps);
}


static void screen_intro_dialog_quit(GtkWidget *w)
{
	GtkWidget *button;
	GtkWidget *quitbox;

	quitbox = gtk_hbox_new(FALSE, 0);
	button = gtk_button_new_with_label ("Quit Perf Studio");
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			          GTK_SIGNAL_FUNC(screen_quit),
				  GTK_OBJECT(sc.screen.dialog_window));

	gtk_box_pack_end(GTK_BOX(quitbox), button, FALSE, FALSE, 10);
	gtk_widget_show_all(quitbox);

	gtk_box_pack_start(GTK_BOX(w), quitbox, FALSE, FALSE, INTRO_FRAME_BORDER);
}


static void screen_intro_dialog_init(struct studio_context *xsc)
{
	GtkWidget *vbox;
	sc.screen.dialog_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	/* if the user quits the app */
	g_signal_connect(G_OBJECT(sc.screen.main_window), "destroy", G_CALLBACK(screen_quit), NULL);
	g_signal_connect(G_OBJECT(sc.screen.main_window), "delete_event", G_CALLBACK(screen_quit), NULL);

	gtk_container_set_border_width(GTK_CONTAINER(sc.screen.dialog_window), 0);
	gtk_window_set_modal((GtkWindow *)sc.screen.dialog_window, TRUE);

	vbox = gtk_vbox_new(FALSE, 0);

	screen_intro_dialog_new_project(vbox);
	screen_intro_dialog_quick_session(vbox);
	screen_intro_dialog_existing_project(xsc, vbox);
	screen_intro_dialog_quit(vbox);

	gtk_widget_show(vbox);

	gtk_container_add(GTK_CONTAINER(sc.screen.dialog_window), vbox);
	gtk_window_set_position((GtkWindow *)sc.screen.dialog_window, GTK_WIN_POS_CENTER);
	gtk_window_present((GtkWindow *)sc.screen.dialog_window);
	gtk_widget_show((GtkWidget *)sc.screen.dialog_window);
}


static gchar *get_path_to_programm(const char *programm)
{
	char *path;
	const char sep[] = ":";
	char *token;
	char full_path[PATH_MAX + 1];

	assert(programm);

	path = getenv("PATH");
	if (!path)
		goto nothing;

	token = strtok(path, sep);
	if (!token)
		goto nothing;

	do {
		snprintf(full_path, PATH_MAX, "%s/%s", token, programm);
		if (!access (full_path, F_OK))
			return g_strdup(full_path);
	} while ((token = strtok(NULL, sep)));


nothing:
	return NULL;
}

#if 0
gint len = 0;
guchar *buffer = 0;
gchar *md5 = 0;

md5 = g_compute_checksum_for_data (G_CHECKSUM_MD5, buffer, len);
g_print ("MD5 checksum of file: %s\n", md5);
g_free (buffer)
#endif


/* FIXME: initialize image after GTK is up */
static void studio_launch(void)
{
	GError *gerror = NULL;
	int exit_status = 0;
	gchar *path_lstopo;
	char *output = NULL;
	gchar *argv[] = { NULL, (gchar *)"--no-legend", (gchar *)"--no-io", (gchar *)"lstopo.png", NULL };

	/*
	 * FIXME:
	 *
	 * skip lstopo and use libhwloc directly
	 * and visualize via cairo.
	 * http://www.open-mpi.org/projects/hwloc/doc/v1.4.2/#interface
	 * */

	path_lstopo = get_path_to_programm("lstopo");
	if (!path_lstopo) {
		pr_err("lstopo(1) not found in PATH, please install hwloc package\n");
		return;
	}

	/* found in path */
	argv[0] = path_lstopo;

	if (!g_spawn_sync(NULL, argv, NULL, 0, NULL, NULL,
		          &output, NULL, &exit_status, &gerror)) {
		pr_err("exec error for %s\n", path_lstopo);
	}

	if (output)
		g_free(output);

	g_free(path_lstopo);
}


static double frand(double fMin, double fMax)
{
	double f = (rand() + 0.0) / RAND_MAX;
	return fMin + f * (fMax - fMin);
}


static void generate_data(void)
{
	int no_cpu, i, j;
	GSList *cpu_list;
	double start , end;

	assert(sc.screen.schart);

	cpu_list = NULL;

	start = frand(0.0, 1.0);
	end   = frand(10.0, 20.0);
	schart_set_data_time_start_end(sc.screen.schart, start, end);


	no_cpu = (rand() % 4) + 4;
	schart_set_data_charts_no(sc.screen.schart, no_cpu);

	for (i = 0; i < no_cpu; i++) {

		struct schart_data_raw_cpu *schart_data_raw_cpu;

		schart_data_raw_cpu = malloc(sizeof(*schart_data_raw_cpu));
		schart_data_raw_cpu->cpu = i;
		schart_data_raw_cpu->events = NULL;

		for (j = 0; j < EVENT_TYPE_MAX; j++) {

			int no_events;
			double eventtime;
			struct schart_data_raw_events *schart_data_raw_events;

			schart_data_raw_events = malloc(sizeof(*schart_data_raw_events));
			schart_data_raw_events->event = j;

			no_events = 0;
			eventtime = start;
			schart_data_raw_events->schart_event = NULL;

			/* generate data */
			do {
				eventtime += frand(0.0, 0.05);

				if (eventtime >= end) {
					schart_data_raw_events->event_size = no_events + 1;
					break;
				}

				schart_data_raw_events->schart_event = realloc(schart_data_raw_events->schart_event, sizeof(struct schart_event) * (no_events + 2));

				schart_data_raw_events->schart_event[no_events].time = eventtime;

				no_events++;
			} while (1);


			schart_data_raw_cpu->events = g_slist_append(schart_data_raw_cpu->events, schart_data_raw_events);
		}

		cpu_list = g_slist_append(cpu_list, schart_data_raw_cpu);
	}

	schart_set_raw_data(sc.screen.schart, cpu_list);
}


static void screen_load_theme(void)
{
#if 0
	GtkCssProvider *provider;
	GdkDisplay *display;
	GdkScreen *screen;
	GError *gerror;
	gboolean ret;


	provider = gtk_css_provider_new();

	display = gdk_display_get_default ();

	screen = gdk_display_get_default_screen (display);

	gtk_style_context_add_provider_for_screen (screen, GTK_STYLE_PROVIDER (provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	ret = gtk_css_provider_load_from_path(css_provider, "/tmp/s.css", &gerror);
	if (ret == false) {
		fprintf(stderr, "Cannot load css file\n");
	}

	g_object_unref (provider);
#endif
}

static void modules_register(struct studio_context *xsc)
{
	module_overview_init(xsc);
	module_example_init(xsc);
}


static int gmain(int ac, const char **av)
{
	int ret;
	const char *report_arg[3] = {"report", "--studio", ""};

	sc.screen.theme = THEME_COLOR_STANDARD;

	if (studio_init()) {
		pr_err("Cannot initialize basic structures\n");
		return EXIT_FAILURE;
	}

	if ((ret = db_global_init())) {
		pr_err("Cannot initialize local database\n");
		return EXIT_FAILURE;
	}

	pr_debug("starting perf studio - 2012\n");

	/* register all modules */
	modules_register(&sc);

	studio_launch();
	init_sighandler();

	/* init database */
	db_get_last_project_path();

	/* gtk init stuff starts here */
	screen_init(ac, av);
	screen_basic_layout_init();

	/* setup main widget screen */
	screen_modern_logo();
	screen_menu_init();
	studio_main_status_widget_new(&sc, sc.screen.vbox);
	//screen_toolbar_init();
	screen_toolbar_mainwindow_init();
	screen_statusbar_init();


	/* finalize window and start */
	perf_gtk_resize_main_window(sc.screen.main_window);
	gtk_window_set_position(GTK_WINDOW(sc.screen.main_window), GTK_WIN_POS_CENTER);
	gtk_widget_show(sc.screen.vbox);
	gtk_widget_show(sc.screen.main_window);

	screen_intro_dialog_init(&sc);

	screen_load_theme();

	generate_data();

	gdk_threads_enter();
	gtk_main();
	gdk_threads_leave();

	return 0;

	cmd_report(2, report_arg, NULL);

	return 0;
}


static void perf_gtk_show_hists(GtkWidget *window, struct hists *hists)
{
	GType col_types[MAX_COLUMNS];
	GtkCellRenderer *renderer;
	struct sort_entry *se;
	GtkListStore *store;
	struct rb_node *nd;
	u64 total_period;
	GtkWidget *view;
	int col_idx;
	int nr_cols;

	nr_cols = 0;

	/* The percentage column */
	col_types[nr_cols++] = G_TYPE_STRING;

	list_for_each_entry(se, &hist_entry__sort_list, list) {
		if (se->elide)
			continue;

		col_types[nr_cols++] = G_TYPE_STRING;
	}

	store = gtk_list_store_newv(nr_cols, col_types);

	view = gtk_tree_view_new();

	renderer = gtk_cell_renderer_text_new();

	col_idx = 0;

	/* The percentage column */
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
						    -1, "Overhead (%)",
						    renderer, "text",
						    col_idx++, NULL);

	list_for_each_entry(se, &hist_entry__sort_list, list) {
		if (se->elide)
			continue;

		gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),
							    -1, se->se_header,
							    renderer, "text",
							    col_idx++, NULL);
	}

	gtk_tree_view_set_model(GTK_TREE_VIEW(view), GTK_TREE_MODEL(store));

	g_object_unref(GTK_TREE_MODEL(store));

	total_period = hists->stats.total_period;

	for (nd = rb_first(&hists->entries); nd; nd = rb_next(nd)) {
		struct hist_entry *h = rb_entry(nd, struct hist_entry, rb_node);
		GtkTreeIter iter;
		double percent;
		char s[512];

		if (h->filtered)
			continue;

		gtk_list_store_append(store, &iter);

		col_idx = 0;

		percent = (h->period * 100.0) / total_period;

		snprintf(s, ARRAY_SIZE(s), "%.2f", percent);

		gtk_list_store_set(store, &iter, col_idx++, s, -1);

		list_for_each_entry(se, &hist_entry__sort_list, list) {
			if (se->elide)
				continue;

			se->se_snprintf(h, s, ARRAY_SIZE(s),
					hists__col_len(hists, se->se_width_idx));

			gtk_list_store_set(store, &iter, col_idx++, s, -1);
		}
	}

	gtk_container_add(GTK_CONTAINER(window), view);
}

int perf_evlist__studio_browse_hists(struct perf_evlist *evlist,
				  const char *help __used,
				  void (*timer) (void *arg)__used,
				  void *arg __used, int delay_secs __used)

{
	struct perf_evsel *pos;

	(void) help;
	(void) timer;
	(void) arg;
	(void) delay_secs;

	list_for_each_entry(pos, &evlist->entries, node) {
		struct hists *hists = &pos->hists;
		//const char *evname = event_name(pos);

		perf_gtk_show_hists(sc.screen.sw, hists);
	}

	return 0;
}


int cmd_studio(int argc, const char **argv, const char *prefix __used)
{
	int ret;

	argc = parse_options(argc, argv, options, probe_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	if (argc > 0) {
		if (strcmp(argv[0], "-") == 0) {
			pr_warning("  Error: '-' is not supported.\n");
			usage_with_options(probe_usage, options);
		}
		ret = parse_probe_event_argv(argc, argv);
		if (ret < 0) {
			pr_err("  Error: Parse Error.  (%d)\n", ret);
			return ret;
		}
	}

	return gmain(argc, argv);

	if (params.max_probe_points == 0)
		params.max_probe_points = MAX_PROBES;

	if ((!params.nevents && !params.dellist && !params.list_events &&
	     !params.show_lines))
		usage_with_options(probe_usage, options);

	/*
	 * Only consider the user's kernel image path if given.
	 */
	symbol_conf.try_vmlinux_path = (symbol_conf.vmlinux_name == NULL);

	if (params.list_events) {
		if (params.mod_events) {
			pr_err("  Error: Don't use --list with --add/--del.\n");
			usage_with_options(probe_usage, options);
		}
		if (params.show_lines) {
			pr_err("  Error: Don't use --list with --line.\n");
			usage_with_options(probe_usage, options);
		}
		ret = show_perf_probe_events();
		if (ret < 0)
			pr_err("  Error: Failed to show event list. (%d)\n",
			       ret);
		return ret;
	}

#ifdef DWARF_SUPPORT
	if (params.show_lines) {
		if (params.mod_events) {
			pr_err("  Error: Don't use --line with"
			       " --add/--del.\n");
			usage_with_options(probe_usage, options);
		}

		ret = show_line_range(&params.line_range, params.target);
		if (ret < 0)
			pr_err("  Error: Failed to show lines. (%d)\n", ret);
		return ret;
	}
#endif

	if (params.dellist) {
		ret = del_perf_probe_events(params.dellist);
		strlist__delete(params.dellist);
		if (ret < 0) {
			pr_err("  Error: Failed to delete events. (%d)\n", ret);
			return ret;
		}
	}

	if (params.nevents) {
		ret = add_perf_probe_events(params.events, params.nevents,
					    params.max_probe_points,
					    params.target,
					    params.force_add);
		if (ret < 0) {
			pr_err("  Error: Failed to add events. (%d)\n", ret);
			return ret;
		}
	}
	return 0;
}


