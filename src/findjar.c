/*
The MIT License (MIT) 
Copyright (c) 2009,2010,2016 Andreas Heck <aheck@gmx.de>

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <sqlite3.h>
#include <zip.h>

#include <classreader/javaclass.h>

static gboolean verbose = FALSE;

static GOptionEntry options[] = 
{
    {"verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose, "Return the full names of all completion suggestions"},
    {NULL}
};

void search_jar(const gchar *filename, const gchar *searchname,
        const gchar *suffix)
{
    struct zip *jar = NULL;
    int errorp = 0;

    jar = zip_open(filename, 0, &errorp);
    if (jar == NULL) {
        fprintf(stderr, "Failed to open '%s'\n", filename);
        return;
    }

    int numfiles = zip_get_num_files(jar);

    for (int i = 0; i < numfiles; i++) {
        const gchar *classfile = zip_get_name(jar, i, 0);
        if (classfile == NULL) continue;
        if (!g_str_has_suffix(classfile, ".class")) continue;
        if (g_strrstr(classfile, "$") != NULL) continue; // skip inner classes

        if (g_strcmp0(classfile, searchname) == 0) {
            fprintf(stdout, "%s %s\n", filename, classfile);
        } else if (g_str_has_suffix(classfile, suffix)) {
            fprintf(stdout, "%s %s\n", filename, classfile);
        }
    }

    zip_close(jar);
}

void search_dir(const gchar *dirname, const gchar *searchname,
        const gchar *suffix)
{
    GDir *dir = NULL;
    const gchar *name = NULL;
    gchar *filename = NULL;
    GError *error = NULL;

    dir = g_dir_open(dirname, 0, &error);

    if (error != NULL) {
        fprintf(stderr, "ERROR: %s\n", error->message);
        return;
    }

    while ((name = g_dir_read_name(dir)) != NULL) {
        filename = g_build_filename(dirname, name, NULL);

        if (g_file_test(filename, G_FILE_TEST_IS_DIR)
                && !g_str_has_prefix(name, ".")) {
            search_dir(filename, searchname, suffix);
        } else if (g_str_has_suffix(filename, ".jar")) {
            if (verbose) printf("Searching JAR file %s\n", filename);
            search_jar(filename, searchname, suffix);
        } else if (g_str_has_suffix(filename, ".class")) {
            if (g_strrstr(filename, "$") != NULL) continue; // skip inner classes

            GError *error = NULL;
            JavaClass *javaclass = javaclass_new_from_file(filename, FALSE, &error);

            if (error != NULL) {
                fprintf(stderr, "ERROR: %s\n", error->message);
                javaclass_free(javaclass);
            } else {
                GString *buffer = g_string_new(javaclass_get_name(javaclass));
                if (javaclass_get_package(javaclass) != NULL) {
                    g_string_prepend(buffer, ".");
                    g_string_prepend(buffer, javaclass_get_package(javaclass));
                }

                if (g_strcmp0(buffer->str, searchname) == 0) {
                    fprintf(stdout, "%s %s\n", filename, searchname);
                } else if (g_str_has_suffix(buffer->str, suffix)) {
                    fprintf(stdout, "%s %s\n", filename, searchname);
                }

                g_string_free(buffer, TRUE);
                javaclass_free(javaclass);
            }
        }
    }

    g_dir_close(dir);
}

void usage(gchar *errormsg, GOptionContext *context)
{
    if (errormsg != NULL) fprintf(stderr, "ERROR: %s\n", errormsg);
    fprintf(stderr, "%s", g_option_context_get_help(context, TRUE, NULL));

    exit(2);
}

int main(int argc, char **argv)
{
    GError *error = NULL;
    GOptionContext *context;

    context = g_option_context_new(
            "classname - Find JAR files that contain a given Java class");
    g_option_context_add_main_entries (context, options, NULL);

    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        usage(error->message, context);
    }

    if (argc < 2) {
        usage(NULL, context);
    }

    const gchar *searchname = argv[1];
    gboolean qualified = FALSE;

    GString *suffix = g_string_new("");

    // If the searchname is an unqualified classname like 'Object' the prefix
    // becomes something like '/Object.class'.
    // If the searchname is a qualified classname like 'java.lang.Object' the
    // prefix becomes something like 'java/lang/Object.class', instead.
    g_string_assign(suffix, searchname);

    gchar *cur = suffix->str;
    while (*cur) {
        if (*cur == '.') {
            qualified = TRUE;
            *cur = '/';
        }
        cur++;
    }

    if (!qualified) {
        g_string_prepend(suffix, "/");
    }

    g_string_append(suffix, ".class");

    search_dir(".", searchname, suffix->str);

    g_string_free(suffix, TRUE);
}
