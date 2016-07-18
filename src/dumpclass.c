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
#include <glib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <global.h>
#include <classreader/javaclass.h>

void usage(gchar *binary)
{
    fprintf(stderr, "Usage: %s <CLASSFILE>\n", binary);
    exit(2);
}

int main(int argc, char** argv)
{
    JavaClass *c = NULL;
    FILE *fp = NULL;
    guchar *contents = NULL;
    int filesize = 0;
    int nbytes = 0;
    struct stat buffer;
    guint32 status;
    gchar *classfile = NULL;
    gchar *access = NULL;
    gchar *type = NULL;

    if (argc != 2) {
        usage(argv[0]);
    }

    classfile = argv[1];

    printf("Reading file '%s'...\n", classfile);

    fp = fopen(classfile, "r");

    if (fp == NULL) {
        fprintf(stderr, "Failed to open file '%s'\n", classfile);
        return 1;
    }

    // get the size of the file
    status = fstat(fileno(fp), &buffer);
    filesize = buffer.st_size;

    contents = g_new(guchar, filesize);

    nbytes = fread(contents, 1, filesize, fp);
    if (nbytes != filesize) {
        fprintf(stderr, "ERROR: Read error! Requested %d bytes but got %d!\n",
                filesize, nbytes);
        return 1;
    }

    fclose(fp);

    GError *error = NULL;
    c = javaclass_new(contents, filesize, FALSE, &error);

    if (error != NULL) {
        fprintf(stderr, "Failed to read the class file %s\n", classfile);
        fprintf(stderr, "%s", error->message);
        g_error_free(error);
        return 1;
    }

    if (javaclass_is_public(c))
        access = "public";
    else
        access = "package";

    if (javaclass_is_interface(c))
        type = "interface";
    else if (javaclass_is_abstract(c))
        type = "abstract class";
    else if (javaclass_is_enum(c))
        type = "enum";
    else if (javaclass_is_annotation(c))
        type = "annotation";
    else
        type = "class";

    const gchar *package = javaclass_get_package(c);
    if (package == NULL) package = DEFAULT_PACKAGE;
    const gchar *class_signature = javaclass_get_signature(c);
    if (class_signature == NULL) class_signature = "(none)";

    printf("Classname: %s\n", javaclass_get_name(c));
    printf("Class signature: %s\n", class_signature);
    printf("Package: %s\n", package);
    printf("Fully-qulified classname: %s\n", javaclass_get_fq_name(c));
    printf("Parent class: %s\n", javaclass_get_fq_parent(c));
    printf("Access: %s\n", access);
    printf("Type: %s\n", type);
    printf("Final: %s\n", javaclass_is_final(c) ? "yes" : "no");
    printf("Classfile version number: %d.%d\n", javaclass_get_major_version_number(c),
            javaclass_get_minor_version_number(c));
    printf("Classfile version: %s\n", javaclass_get_version_name(c));
    printf("Interfaces count: %d\n", javaclass_get_interface_number(c));

    // print interfaces if there are any
    if (javaclass_get_interface_number(c) > 0) {
        printf("Interfaces:\n");
        gchar **interfaces = javaclass_get_interfaces(c);

        for (int i = 0; interfaces[i]; i++) {
            printf("    %s\n", interfaces[i]);
        }
    }

    printf("Fields count: %d\n", javaclass_get_field_number(c));

    if (javaclass_get_field_number(c) > 0) {
        printf("Fields:\n");
        JavaField **fields = javaclass_get_fields(c);
        GString *fielddef = g_string_new("");

        for (int i = 0; fields[i]; i++) {
            printf("    ");

            //printf("%s\n", fields[i]);

            const gchar *sig = javafield_get_signature(fields[i]);
            if (sig != NULL) printf("    Signature: %s\n", sig);
        }

        g_string_free(fielddef, TRUE);
    }

    printf("Methods count: %d\n", javaclass_get_method_number(c));

    if (javaclass_get_method_number(c) > 0) {
        printf("Methods:\n");
        JavaMethod **methods = javaclass_get_methods(c);
        GString *methoddef = g_string_new("");

        for (int i = 0; methods[i]; i++) {
            //printf("    %s\n", methods[i]);

            gchar **exceptions = javamethod_get_exceptions(methods[i]);
            if (exceptions != NULL) {
                printf("        throws");
                for (int i = 0; exceptions[i]; i++) {
                    printf(" %s", exceptions[i]);
                }
                printf("\n");
            }

            const gchar *sig = javamethod_get_signature(methods[i]);
            if (sig != NULL) printf("    Signature: %s\n", sig);
        }

        g_string_free(methoddef, TRUE);
    }

    javaclass_free(c);

    g_free(contents);

    return 0;
}
