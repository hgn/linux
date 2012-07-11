/*
 * studio-db.c
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

#include <glib.h>
#include <glib/gstdio.h>

#include "perf.h"
#include "builtin.h"

#include "studio-assistant.h"
#include "studio-db.h"

#define DB_GLOBAL_PATH ".perf-studio-conf.xml"

#define DB_DEFAULT_BACKLOG_MAX_SIZE "536870912"  /* 2^29 byte -> 2^30/2 byte) */
#define DB_DEFAULT_BACKLOG_MAX_NUMBER "10"


extern struct studio_context sc;


static xmlDocPtr db_generic_open_global_conf(void)
{
	assert(sc.db.global_conf_path);
	return xmlParseFile(sc.db.global_conf_path);
}


static void db_generic_close_global_conf(xmlDocPtr doc)
{
	xmlFreeDoc(doc);
}

static void readxmlconf(void)
{
}


#if 0
static gboolean db_global_add_project(struct db_project_summary *ps)
{
}

static gboolean db_project_new(void)
{
	/* create new XML file */

	/* add project to global database */
	db_global_add_project();

}
#endif

gboolean db_global_remove_project_path(const char *path)
{
	xmlNodePtr no;
	xmlNodePtr cell, next;

	for (no = cell->children; no;) {
		next = no->next;
		xmlUnlinkNode(no);
		no=next;
	}
}

static bool db_local_get_name(struct studio_assitant_new_project_data *pd)
{
}

static bool db_local_get_summary(const char *project_file_path, struct db_project_summary *ps)
{
	xmlDocPtr doc;
	xmlNodePtr level1, level2, level3;
	xmlChar *value;

	doc = xmlParseFile(project_file_path);
	if (!doc) {
		pr_err("Cannot open project file: %s\n", project_file_path);
		return false;
	}

	ps->name = g_strdup((gchar *)project_file_path);
	ps->path = g_strdup((gchar *)project_file_path);

	if (!doc->children) {
		pr_err("Configuration files is corrupted\n");
		xmlFreeDoc(doc);
		return false;
	}


	/* skip root level */
	level1 = doc->children;

	for (level2 = level1->children;
	     level2 != NULL;
	     level2 = level2->next) {


		if (xmlStrEqual(level2->name, BAD_CAST "last-accessed")) {
			value = xmlNodeGetContent(level2);
			ps->last_accessed = g_strdup((gchar *)value);
			xmlFree(value);
		}

		if (xmlStrEqual(level2->name, BAD_CAST "name")) {
			value = xmlNodeGetContent(level2);
			ps->name = g_strdup((gchar *)value);
			xmlFree(value);
		}
	}

	xmlFreeDoc(doc);

	return true;
}



/* Return false if something went wrong
 * or when no project is in the database
 */
gboolean db_generic_get_projects_summaries(struct db_projects_summary **xps)
{
	bool ret;
	xmlDocPtr doc;
	xmlNodePtr nodeLevel1;
	xmlNodePtr nodeLevel2;
	xmlNodePtr nodeLevel3;
	xmlNodePtr nodeLevel4;
	struct db_projects_summary *ps;
	struct db_project_summary *pss;
	xmlChar *project_path;

	ps = NULL;
	*xps = NULL;

	ret = true;

	doc = db_generic_open_global_conf();
	if (!doc) {
		pr_err("Could not open global configuration file\n");
		return false;
	}

	if (!doc->children) {
		pr_err("Configuration files is corrupted\n");
		db_generic_close_global_conf(doc);
		return false;
	}


	/* skip root level */
	nodeLevel1 = doc->children;

	for (nodeLevel2 = nodeLevel1->children;
	     nodeLevel2 != NULL;
	     nodeLevel2 = nodeLevel2->next) {


		if (!xmlStrEqual(nodeLevel2->name, BAD_CAST "projects"))
			continue;

		for (nodeLevel3 = nodeLevel2->children;
		     nodeLevel3 != NULL;
		     nodeLevel3 = nodeLevel3->next) {



			if (!xmlStrEqual(nodeLevel3->name, BAD_CAST "project"))
				continue;

			for (nodeLevel4 = nodeLevel3->children;
			     nodeLevel4 != NULL;
			     nodeLevel4 = nodeLevel4->next) {


				if (xmlStrEqual(nodeLevel4->name, BAD_CAST "path")) {
					project_path = xmlNodeGetContent(nodeLevel4);
					pss = g_malloc0(sizeof(*pss));
					ret = db_local_get_summary(project_path, pss);
					if (ret != true) {
						pr_err("Failed to parse local project file (%s)\n",
								project_path);
						xmlFree(project_path);
						if (ps)
							db_generic_get_projects_summary_free(ps);
						*xps = NULL;
						return false;
					}

					xmlFree(project_path);

					if (!ps) {
						ps = g_malloc(sizeof(*ps));
						ps->list = NULL;
					}

					ps->list = g_slist_append(ps->list, pss);

				}
			}
		}
	}

	*xps = ps;
	db_generic_close_global_conf(doc);

	return ret;
}


void db_generic_get_projects_summary_free(struct db_projects_summary *ps)
{
	GSList *tmp;

	assert(ps);

	tmp = ps->list;
	while (tmp != NULL) {
		struct db_project_summary *db_project_summary;

		db_project_summary = tmp->data;

		g_free(db_project_summary->name);
		g_free(db_project_summary->path);
		g_free(db_project_summary->last_accessed);
		g_free(db_project_summary);

		/// FIXME: free list head element

		tmp = g_slist_next(tmp);
	}

	g_free(ps);
}


static xmlNodePtr get_project_root(xmlDocPtr doc)
{
	xmlNodePtr nodeLevel1, nodeLevel2;

	assert(doc);

	if (!doc->children)
		return NULL;

	nodeLevel1 = doc->children;

	for (nodeLevel2 = nodeLevel1->children;
	     nodeLevel2 != NULL;
	     nodeLevel2 = nodeLevel2->next) {

		if (!xmlStrEqual(nodeLevel2->name, BAD_CAST "projects"))
			continue;

		fprintf(stderr, "-> %s\n", nodeLevel2->name);


		return nodeLevel2;
	}

	return NULL;
}



static bool db_global_generate_database_templace(const char *path)
{
	xmlDocPtr doc;
	xmlNodePtr root_node, node, node1, node_statistic, gui_configuration;
	char time_str[256];
	time_t t;
	struct tm *tmp;

	doc = NULL;
	root_node = node = node1 = NULL;

	doc = xmlNewDoc(BAD_CAST "1.0");
	root_node = xmlNewNode(NULL, BAD_CAST "perf-studio");
	xmlDocSetRootElement(doc, root_node);

	xmlNewChild(root_node, NULL, BAD_CAST "projects", NULL);

	node_statistic = xmlNewChild(root_node, NULL, BAD_CAST "statistics", NULL);
	xmlNewChild(node_statistic, NULL, BAD_CAST "times-executed", BAD_CAST "1");


	/* save first time accessed */
	t = time(NULL);
	if (t == (time_t) -1) {
		pr_warning("Cannot determine current time via time(1)\n");
		t = 0;
	}
	tmp = localtime(&t);
	if (tmp == NULL) {
		pr_err("localtime: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (strftime(time_str, sizeof(time_str), "%s", tmp) == 0) {
		fprintf(stderr, "strftime returned 0");
		exit(EXIT_FAILURE);
	}


	xmlNewChild(node_statistic, NULL, BAD_CAST "first-time-executed", BAD_CAST time_str);
	xmlNewChild(node_statistic, NULL, BAD_CAST "last-time-executed", BAD_CAST time_str);

	xmlNewChild(node_statistic, NULL, BAD_CAST "trace-backlog-max-byte-history", BAD_CAST DB_DEFAULT_BACKLOG_MAX_SIZE);
	xmlNewChild(node_statistic, NULL, BAD_CAST "trace-backlog-max-number", BAD_CAST DB_DEFAULT_BACKLOG_MAX_NUMBER);

	/* gui configuration */
	gui_configuration = xmlNewChild(root_node, NULL, BAD_CAST "gui-configuration", NULL);
	xmlNewChild(gui_configuration, NULL, BAD_CAST "theme", BAD_CAST "dark");

	/* placeholder */
	xmlNewChild(node_statistic, NULL, BAD_CAST "last-project-name", NULL);

	xmlSaveFormatFileEnc(path, doc, "UTF-8", 1);
	xmlFreeDoc(doc);

	return true;
}


/* this adds the path to the global db. If the project
 * is already in the database it is ignored */
static bool db_global_add_project_path(char *conf_path, struct studio_assitant_new_project_data *pd)
{
	bool ret;
	xmlDocPtr doc;
	xmlNodePtr project_doc, project_node;

	assert(sc.db.global_conf_path);

	ret = true;
	doc = db_generic_open_global_conf();
	if (!doc) {
		pr_err("Cannot parse global database: %s\n", sc.db.global_conf_path);
		return false;
	}


	project_doc = get_project_root(doc);
	if (!project_doc) {
		pr_err("Database seems corrupt (%s), please fix\n",
			sc.db.global_conf_path);
		xmlFreeDoc(doc);
		ret = false;
		goto out;
	}

	project_node = xmlNewChild(project_doc, NULL, BAD_CAST "project", NULL);
	xmlNewProp(project_node, BAD_CAST "active", BAD_CAST "yes");

	xmlNewChild(project_node, NULL, BAD_CAST "path", BAD_CAST conf_path);

	xmlSaveFormatFileEnc(sc.db.global_conf_path, doc, "UTF-8", 1);

out:
	db_generic_close_global_conf(doc);

	return ret;
}


static void add_new_child_time(xmlNodePtr ptr, const char *child_name)
{
	char time_str[256];
	time_t t;
	struct tm *tmp;

	/* save first time accessed */
	t = time(NULL);
	if (t == (time_t) -1) {
		pr_warning("Cannot determine current time via time(1)\n");
		t = 0;
	}
	tmp = localtime(&t);
	if (tmp == NULL) {
		pr_err("localtime: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (strftime(time_str, sizeof(time_str), "%s", tmp) == 0) {
		fprintf(stderr, "strftime returned 0");
		exit(EXIT_FAILURE);
	}


	xmlNewChild(ptr, NULL, BAD_CAST child_name, BAD_CAST time_str);
}

#if 0
traces
	trace
		file-path
		id type=sha1
		events
			event 1
			event 2
			event ..
		events
	trace
traces
#endif

void perf_project_free(struct perf_project *pd)
{
	assert(pd);

	g_free(pd->executable_path);
	g_free(pd->working_dir);
	g_free(pd);
	pd = NULL;
}


bool db_local_get_perf_project(struct studio_context *sc, gchar *project_path)
{
	xmlDocPtr doc;
	xmlNodePtr level1, level2, level3;
	xmlChar *value;
	struct perf_project *xpd;

	assert(sc);
	assert(project_path);

	if (access(project_path, F_OK)) {
		pr_err("Database curruption for project path: %s\n", project_path);
		return false;
	}

	doc = xmlParseFile(project_path);
	if (!doc) {
		pr_err("Cannot open project file: %s\n", project_path);
		return false;
	}

	if (!doc->children) {
		pr_err("Configuration files is corrupted\n");
		xmlFreeDoc(doc);
		return false;
	}

	xpd = g_malloc0(sizeof(*xpd));
	xpd->sc = sc;

	/* skip root level */
	level1 = doc->children;

	for (level2 = level1->children;
	     level2 != NULL;
	     level2 = level2->next) {

		if (xmlStrEqual(level2->name, BAD_CAST "name")) {
			value = xmlNodeGetContent(level2);
			xpd->name = g_strdup((gchar *)value);
			xmlFree(value);
		}

		if (xmlStrEqual(level2->name, BAD_CAST "executable-path")) {
			value = xmlNodeGetContent(level2);
			xpd->executable_path = g_strdup((gchar *)value);
			xmlFree(value);
		}

		if (xmlStrEqual(level2->name, BAD_CAST "working-dir")) {
			value = xmlNodeGetContent(level2);
			xpd->working_dir = g_strdup((gchar *)value);
			xmlFree(value);
		}
	}


	xmlFreeDoc(doc);

	sc->perf_project_data = xpd;

	return true;
}




/*
 * <perf-studio-project>
 *  <executable-path>/sbin/ls</executable-path>
 *  <executable-arguments>foo bar</executable-arguments>
 *  <working-dir>foo bar</working-dir>
 *  <last-lession id="" />
 *
 *  <sessions>
 *   <session>
 *    <date value="3333333" />
 *    <id value="652525626246246" />
 *    <records>
 *     <record>
 *      <module name="overview" />
 *      <executable-checksum method="md5">4283243242</executable-checksum>
 *      <traced-cores>ALL</traced-cores>
 *      <path value="/foo/perf.data.3333333" />
 *      <traced-events>
 *       <event type="standard">
 *         <name>foo</name>
 *         <sav value="PERF_DEFAULT" />
 *         <modifiers value="u" />
 *        </event>
 *       </traced-events>
 *      </record>
 *     </records>
 *    </session>
 *   </sessions>
 * </perf-studio-project>
 */
void db_local_generate_project_file(struct studio_assitant_new_project_data *pd)
{
	bool ret;
	xmlDocPtr doc;
	xmlNodePtr root_node, node, node1, last_session_node;
	gchar *dirname;
	gchar conf_path[PATH_MAX];
	gchar perf_data_path[PATH_MAX];

	doc = NULL;
	root_node = node = node1 = NULL;


	fprintf(stderr, "project data\n");
	fprintf(stderr, "\tproject name:    %s\n", pd->project_name);
	fprintf(stderr, "\texecutable path: %s\n", pd->executable_path);

	assert(pd->executable_path);

	dirname = g_path_get_dirname(pd->executable_path);
	assert(dirname);

	g_snprintf(conf_path, PATH_MAX - 1, "%s/%s", dirname, ".perf-studio.xml");
	g_snprintf(perf_data_path, PATH_MAX - 1, "%s/%s", dirname, ".perf-studio-data");

	if (!g_file_test(perf_data_path, G_FILE_TEST_EXISTS)) {
		g_print("Creating %s directory\n", perf_data_path);
		g_mkdir(perf_data_path, 0700);
	} else {
		g_print("Directory %s already exists\n", perf_data_path);
	}


	doc = xmlNewDoc(BAD_CAST "1.0");
	root_node = xmlNewNode(NULL, BAD_CAST "perf-studio-project");
	xmlDocSetRootElement(doc, root_node);

	xmlNewChild(root_node, NULL, BAD_CAST "executable-path", BAD_CAST pd->executable_path);
	xmlNewChild(root_node, NULL, BAD_CAST "executable-arguments", BAD_CAST "");
	xmlNewChild(root_node, NULL, BAD_CAST "working-dir", BAD_CAST dirname);
	xmlNewChild(root_node, NULL, BAD_CAST "name", BAD_CAST pd->project_name);

	add_new_child_time(root_node, "last-accessed");


	xmlNewChild(root_node, NULL, BAD_CAST "sessions", NULL);

	ret = db_global_add_project_path(conf_path, pd);
	if (ret != true) {
		pr_err("Cannot add new project path (%s) to global db (%s)\n",
		       conf_path, sc.db.global_conf_path);
		return;
	}

	xmlSaveFormatFileEnc(conf_path, doc, "UTF-8", 1);
	xmlFreeDoc(doc);

	g_free(dirname);

	return;
}



/* db_global_init opens the file and if file
 * is not presented it generates a standard DB
 * and close the file again */
int db_global_init(void)
{
	int ret;
	char db_path[FILENAME_MAX];

	/* test XML library version */
	LIBXML_TEST_VERSION;

	xmlKeepBlanksDefault(0);

	assert(sc.homedirpath);
	assert(DB_GLOBAL_PATH);


	ret = snprintf(db_path, FILENAME_MAX - 1, "%s/%s", sc.homedirpath, DB_GLOBAL_PATH);
	if (ret < (signed)strlen(DB_GLOBAL_PATH)) {
		pr_err("Cannot create db path (%d)\n", ret);
		return -EINVAL;
	}

	if (access(db_path, F_OK)) {
		pr_debug("create initial perf studio database: %s\n", db_path);
		ret = db_global_generate_database_templace(db_path);
		if (ret != true) {
			pr_err("Cannot generate perf database! Exiting now\n");
			exit(EXIT_FAILURE);
		}
	}

	sc.db.global_conf_path = g_strdup(db_path);

	pr_debug("open perf studio database: %s\n", db_path);


	return 0;
}



const char *db_get_last_project_path(void)
{
	return NULL;
}
