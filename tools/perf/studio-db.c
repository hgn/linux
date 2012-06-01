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

#include "perf.h"
#include "builtin.h"

#include "builtin-studio.h"
#include "studio-db.h"

#define DB_GLOBAL_PATH ".perf-studio-conf.xml"

#define DB_DEFAULT_BACKLOG_MAX_SIZE 536870912  /* 2^29 byte -> 2^30/2 byte) */
#define DB_DEFAULT_BACKLOG_MAX_NUMBER 10


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


/* Return false if something went wrong
 * or when no project is in the database
 */
gboolean db_generic_get_projects_summaries(struct db_projects_summary **xps)
{
	xmlDocPtr doc;
	xmlNodePtr nodeLevel1;
	xmlNodePtr nodeLevel2;
	xmlNodePtr nodeLevel3;
	xmlNodePtr nodeLevel4;
	struct db_projects_summary *ps;
	struct db_project_summary *pss;
	xmlChar *c1, *c2, *c3;

	ps = NULL;
	*xps = NULL;

	c1 = c2 = c3 = NULL;


	doc = db_generic_open_global_conf();

	if (!doc) {
		pr_err("Could not open global configuration file\n");
		return false;
	}

	if (!doc->children) {
		pr_err("Configuration files is corrupted\n");
		return false;
	}


	/* skip root level */
	nodeLevel1 = doc->children;



	for (nodeLevel2 = nodeLevel1->children;
	     nodeLevel2 != NULL;
	     nodeLevel2 = nodeLevel2->next) {

		fprintf(stderr, "X\n");


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


				if (xmlStrEqual(nodeLevel4->name, BAD_CAST "name")) {
					c1 = xmlNodeGetContent(nodeLevel4);
				}

				if (xmlStrEqual(nodeLevel4->name, BAD_CAST "path")) {
					c2 = xmlNodeGetContent(nodeLevel4);
				}

				if (xmlStrEqual(nodeLevel4->name, BAD_CAST "last-accessed")) {
					c3 = xmlNodeGetContent(nodeLevel4);
				}
			}

			if (c1 && c2 && c3) {
				if (!ps) {
					ps = g_malloc(sizeof(*ps));
					ps->list = NULL;
				}

				pss = g_malloc(sizeof(*pss));
				pss->name          = g_strdup((gchar *)c1);
				pss->path          = g_strdup((gchar *)c2);
				pss->last_accessed = g_strdup((gchar *)c3);

				ps->list = g_slist_append(ps->list, pss);

				xmlFree(c1); xmlFree(c2); xmlFree(c3);
				c1 = c2 = c3 = NULL;

				continue;
			}

			if (c1 || c2 || c3) {
				pr_err("Database seems corrupt (%s), please fix",
					       sc.db.global_conf_path);
				return false;

			}
		}
	}

	/* c1, c2, c3 SHOULD be freed */
	assert(!c1); assert(!c2); assert(!c3);

	db_generic_close_global_conf(doc);


	*xps = ps;

	return true;
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


static bool db_global_generate_database_templace(const char *path)
{
	xmlDocPtr doc;
	xmlNodePtr root_node, node, node1, node_statistic;
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

	/* placeholder */
	xmlNewChild(node_statistic, NULL, BAD_CAST "last-project-name", NULL);

	xmlSaveFormatFileEnc(path, doc, "UTF-8", 1);
	xmlFreeDoc(doc);

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
bool db_global_generate_project_file(const char *path)
{
	xmlDocPtr doc;
	xmlNodePtr root_node, node, node1, node_statistic, last_session_node;
	char time_str[256];
	time_t t;
	struct tm *tmp;

	doc = NULL;
	root_node = node = node1 = NULL;

	doc = xmlNewDoc(BAD_CAST "1.0");
	root_node = xmlNewNode(NULL, BAD_CAST "perf-studio-project");
	xmlDocSetRootElement(doc, root_node);

	xmlNewChild(root_node, NULL, BAD_CAST "executable-path", BAD_CAST "/fooo");
	xmlNewChild(root_node, NULL, BAD_CAST "executable-arguments", BAD_CAST "arg1 arg2 arg3");
	xmlNewChild(root_node, NULL, BAD_CAST "working-dir", BAD_CAST "/fooo");

	last_session_node = xmlNewChild(root_node, NULL, BAD_CAST "last-lession", NULL);
	xmlNewProp(last_session_node, BAD_CAST "id", BAD_CAST "00c6d13a68eb4b8112dc423e95ab8cae");

	xmlNewChild(root_node, NULL, BAD_CAST "sessions", NULL);

	xmlSaveFormatFileEnc(path, doc, "UTF-8", 1);
	xmlFreeDoc(doc);

	return true;
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
