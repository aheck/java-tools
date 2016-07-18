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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <global.h>
#include <classreader/javaclass.h>

const gchar *DDL = "CREATE TABLE namespaces ("
    "    id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,"
    "    name VARCHAR NOT NULL"
    ");"
    "CREATE TABLE importables ("
    "    id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,"
    "    name VARCHAR NOT NULL"
    ");"
    "CREATE TABLE importables_namespaces ("
    "    importable_id INTEGER,"
    "    namespace_id INTEGER,"
    "    parent_importable_id INTEGER,"
    "    parent_namespace_id INTEGER,"
    "    done BOOLEAN,"
    "    ispublic BOOLEAN,"
    "    isfinal BOOLEAN,"
    "    isinterface BOOLEAN,"
    "    isabstract BOOLEAN,"
    "    isannotation BOOLEAN,"
    "    isenum BOOLEAN,"
    "    signature VARCHAR,"
    "    PRIMARY KEY (importable_id, namespace_id)"
    ");"
    "CREATE TABLE fields ("
    "    id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,"
    "    name VARCHAR NOT NULL,"
    "    descriptor VARCHAR NOT NULL,"
    "    signature VARCHAR,"
    "    importable_id INTEGER,"
    "    namespace_id INTEGER,"
    "    ispublic BOOLEAN,"
    "    isprotected BOOLEAN,"
    "    isprivate BOOLEAN,"
    "    isstatic BOOLEAN,"
    "    isfinal BOOLEAN,"
    "    isenum BOOLEAN"
    ");"
    "CREATE TABLE methods ("
    "    id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,"
    "    name VARCHAR NOT NULL,"
    "    descriptor VARCHAR NOT NULL,"
    "    signature VARCHAR,"
    "    importable_id INTEGER,"
    "    namespace_id INTEGER,"
    "    ispublic BOOLEAN,"
    "    isprotected BOOLEAN,"
    "    isprivate BOOLEAN,"
    "    isstatic BOOLEAN,"
    "    isfinal BOOLEAN,"
    "    issynchronized BOOLEAN,"
    "    isabstract BOOLEAN"
    ");"
    "CREATE TABLE interfaces ("
    "    importable_id INTEGER,"
    "    namespace_id INTEGER,"
    "    interface_importable_id INTEGER,"
    "    interface_namespace_id INTEGER,"
    "    PRIMARY KEY (importable_id, namespace_id, interface_importable_id, "
    "    interface_namespace_id)"
    ");"
    "CREATE TABLE exceptions ("
    "    method_id INTEGER,"
    "    importable_id INTEGER,"
    "    namespace_id INTEGER,"
    "    PRIMARY KEY (method_id, importable_id, namespace_id)"
    ");"
    "CREATE TABLE files ("
    "    id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,"
    "    path VARCHAR,"
    "    filename VARCHAR"
    ");"
    "";

const gchar *INDEXES = "CREATE UNIQUE INDEX IDX_UNIQUE_NAMESPACES "
    "ON namespaces (name);"
    "CREATE UNIQUE INDEX IDX_IMPORTABLES ON importables (name);"
    "CREATE UNIQUE INDEX IDX_UNIQUE_FIELDS ON fields"
    "    (name, importable_id, namespace_id);"
    "CREATE UNIQUE INDEX IDX_UNIQUE_METHODS ON methods "
    "    (name, signature, importable_id, namespace_id);"
    "";

/*
 * Global variables
 */
sqlite3 *db = NULL;

sqlite3_stmt *stmt_insert_namespace       = NULL;
sqlite3_stmt *stmt_insert_class           = NULL;
sqlite3_stmt *stmt_insert_class_namespace = NULL;
sqlite3_stmt *stmt_insert_field           = NULL;
sqlite3_stmt *stmt_insert_method          = NULL;
sqlite3_stmt *stmt_insert_interface       = NULL;
sqlite3_stmt *stmt_insert_exception       = NULL;
sqlite3_stmt *stmt_insert_file            = NULL;
sqlite3_stmt *stmt_is_done                = NULL;
sqlite3_stmt *stmt_set_done               = NULL;
sqlite3_stmt *stmt_set_class_attributes   = NULL;

// hash tables to make sure that the data we insert are unique
GHashTable *inserted_namespaces  = NULL;
GHashTable *inserted_importables = NULL;

// all strings in this program are put into one huge string chunk because
// in the JDK alone there are about 15,000 classes and their packages which
// we need to keep in our hash tables
GStringChunk *strchunk = NULL;

/*
 * Function prototypes
 */
void index_dir(const gchar *dirname, gboolean index_filenames);
void index_jar(gchar *jarfile);
void insert_file(const gchar *path, const gchar *filename);
void process_class(JavaClass *c);
void index_classpath(gchar *classpath);
void create_database();
void create_indexes();
void handle_sql_error(int status, int line);

void cleanup()
{
    if (inserted_namespaces != NULL) {
        g_hash_table_destroy(inserted_namespaces);
    }

    if (inserted_importables != NULL) {
        g_hash_table_destroy(inserted_importables);
    }

    if (strchunk != NULL) {
        g_string_chunk_free(strchunk);
    }

    if (stmt_insert_namespace != NULL) {
        sqlite3_finalize(stmt_insert_namespace);
    }

    if (stmt_insert_class != NULL) {
        sqlite3_finalize(stmt_insert_class);
    }

    if (stmt_insert_class_namespace != NULL) {
        sqlite3_finalize(stmt_insert_class_namespace);
    }

    if (stmt_insert_field != NULL) {
        sqlite3_finalize(stmt_insert_field);
    }

    if (stmt_insert_method != NULL) {
        sqlite3_finalize(stmt_insert_method);
    }

    if (stmt_insert_interface != NULL) {
        sqlite3_finalize(stmt_insert_interface);
    }

    if (stmt_insert_exception != NULL) {
        sqlite3_finalize(stmt_insert_exception);
    }

    if (stmt_insert_file != NULL) {
        sqlite3_finalize(stmt_insert_file);
    }

    if (stmt_is_done != NULL) {
        sqlite3_finalize(stmt_is_done);
    }

    if (stmt_set_done != NULL) {
        sqlite3_finalize(stmt_set_done);
    }

    if (stmt_set_class_attributes != NULL) {
        sqlite3_finalize(stmt_set_class_attributes);
    }
}

int main(int argc, char** argv)
{
    gchar *classpath = NULL;
    gchar *javahome  = NULL;
    gchar *error_msg = NULL;
    int status = 0;

    atexit(cleanup);

    create_database();
    sqlite3_extended_result_codes(db, 1);

    // prepare all the SQL statements needed by the other functions
    status = sqlite3_prepare_v2(db,
            "INSERT INTO namespaces (name) VALUES (?);",
            -1, &stmt_insert_namespace, NULL);
    handle_sql_error(status, __LINE__);

    status = sqlite3_prepare_v2(db,
            "INSERT INTO importables (name) VALUES (?);",
            -1, &stmt_insert_class, NULL);
    handle_sql_error(status, __LINE__);

    status = sqlite3_prepare_v2(db,
            "INSERT INTO importables_namespaces "
            "(importable_id, namespace_id, done) "
            "VALUES (?, ?, ?)",
            -1, &stmt_insert_class_namespace, NULL);
    handle_sql_error(status, __LINE__);

    status = sqlite3_prepare_v2(db,
            "INSERT INTO fields "
            "(name, descriptor, signature, importable_id, namespace_id, "
            "ispublic, isprotected, isprivate, isstatic, isfinal, isenum) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            -1, &stmt_insert_field, NULL);
    handle_sql_error(status, __LINE__);

    status = sqlite3_prepare_v2(db,
            "INSERT INTO methods "
            "(name, descriptor, signature, importable_id, namespace_id, "
            "ispublic, isprotected, isprivate, isstatic, isfinal, "
            "issynchronized, isabstract) VALUES "
            "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            -1, &stmt_insert_method, NULL);
    handle_sql_error(status, __LINE__);

    status = sqlite3_prepare_v2(db,
            "INSERT INTO interfaces "
            "(importable_id, namespace_id, interface_importable_id, "
            "interface_namespace_id) VALUES (?, ?, ?, ?)",
            -1, &stmt_insert_interface, NULL);
    handle_sql_error(status, __LINE__);

    status = sqlite3_prepare_v2(db,
            "INSERT INTO exceptions "
            "(method_id, importable_id, namespace_id) VALUES "
            "(?, ?, ?)",
            -1, &stmt_insert_exception, NULL);
    handle_sql_error(status, __LINE__);

    status = sqlite3_prepare_v2(db,
            "INSERT INTO files "
            "(path, filename) VALUES "
            "(?, ?)",
            -1, &stmt_insert_file, NULL);
    handle_sql_error(status, __LINE__);

    status = sqlite3_prepare_v2(db,
            "SELECT done FROM importables_namespaces WHERE importable_id=? "
            "AND namespace_id=?",
            -1, &stmt_is_done, NULL);
    handle_sql_error(status, __LINE__);

    status = sqlite3_prepare_v2(db,
            "UPDATE importables_namespaces SET done=1 WHERE importable_id=? "
            "AND namespace_id=?",
            -1, &stmt_set_done, NULL);
    handle_sql_error(status, __LINE__);

    status = sqlite3_prepare_v2(db,
            "UPDATE importables_namespaces SET parent_importable_id=?, "
            "parent_namespace_id=?, ispublic=?, isfinal=?, "
            "isinterface=?, isabstract=?, isannotation=?, isenum=?, "
            "signature=?"
            " WHERE importable_id=? AND namespace_id=?",
            -1, &stmt_set_class_attributes, NULL);
    handle_sql_error(status, __LINE__);

    status = sqlite3_exec(db, "BEGIN TRANSACTION", NULL, 0, &error_msg);

    inserted_namespaces  = g_hash_table_new_full(g_str_hash, g_str_equal,
            NULL, g_free);
    inserted_importables = g_hash_table_new_full(g_str_hash, g_str_equal,
            NULL, g_free);

    strchunk = g_string_chunk_new(64);

    classpath = g_strdup(g_getenv("CLASSPATH"));
    javahome  = g_strdup(g_getenv("JAVA_HOME"));

    if (classpath != NULL) {
        index_classpath(classpath);
    }

    if (javahome != NULL) {
        index_dir(javahome, FALSE);
    } else {
        fprintf(stderr, "JDK classes can't be indexed since JAVA_HOME is not set\n");
    }

    index_dir(".", TRUE);

    g_free(classpath);
    g_free(javahome);

    create_indexes();

    status = sqlite3_exec(db, "COMMIT", NULL, 0, &error_msg);
    sqlite3_close(db);
}

/*
 * Index a directory and all its subdirectories
 */
void index_dir(const gchar *dirname, gboolean index_filenames)
{
    GDir *dir = NULL;
    const gchar *name = NULL;
    gchar *fullname = NULL;
    GError *error = NULL;

    dir = g_dir_open(dirname, 0, &error);

    if (error != NULL) {
        fprintf(stderr, "ERROR: %s\n", error->message);
        return;
    }

    while ((name = g_dir_read_name(dir)) != NULL) {
        fullname = g_build_filename(dirname, name, NULL);

        if (index_filenames == TRUE) {
            if (g_file_test(fullname, G_FILE_TEST_IS_REGULAR)
                    && g_strcmp0(DB_FILE, name) != 0) {
                insert_file(dirname, name);
            }
        }

        if (g_file_test(fullname, G_FILE_TEST_IS_DIR)
                && !g_str_has_prefix(name, ".")) {
            index_dir(fullname, index_filenames);
        } else if (g_str_has_suffix(fullname, ".class") &&
                g_strrstr(fullname, "$") == NULL) {
            GError *error = NULL;
            JavaClass *javaclass = javaclass_new_from_file(fullname, FALSE, &error);

            if (error != NULL) {
                fprintf(stderr, "%s\n", error->message);
                javaclass_free(javaclass);
            } else {
                process_class(javaclass);
            }

            javaclass_free(javaclass);
        } else if (g_str_has_suffix(fullname, ".jar")) {
            index_jar(fullname);
        }

        g_free(fullname);
    }

    g_dir_close(dir);
}

/*
 * Index the contents of a JAR file
 */
void index_jar(gchar *jarfile)
{
    struct zip *jar = NULL;
    int errorp = 0;
    const gchar *filename = NULL;
    int numfiles = 0;
    struct zip_stat buffer;
    guint32 filesize = 0;
    guchar *classbytes = NULL;
    struct zip_file *fp = NULL;
    
    jar = zip_open(jarfile, 0, &errorp);
    if (jar == NULL) {
        fprintf(stderr, "Failed to open '%s'\n", jarfile);
        return;
    }

    numfiles = zip_get_num_files(jar);

    for (int i = 0; i < numfiles; i++) {
        filename = zip_get_name(jar, i, 0);
        if (filename == NULL) continue;
        if (!g_str_has_suffix(filename, ".class")) continue;
        if (g_strrstr(filename, "$") != NULL) continue; // skip inner classes

        zip_stat_index(jar, i, 0, &buffer);
        filesize = buffer.size;
        classbytes = g_malloc(filesize);

        fp = zip_fopen_index(jar, i, 0);
        int nbytes = zip_fread(fp, classbytes, filesize);
        zip_fclose(fp);

        if (filesize != nbytes) {
            fprintf(stderr, "ERROR: Read error! Requested %d bytes but got %d!\n",
                    filesize, nbytes);
            g_free(classbytes);

            continue;
        }

        GError *error = NULL;
        JavaClass *javaclass = javaclass_new(classbytes, filesize, FALSE, &error);
        g_free(classbytes);

        if (error == NULL) {
            process_class(javaclass);
        } else {
            fprintf(stderr, "ERROR: %s\n", error->message);
        }
    }

    zip_close(jar);
}

/*
 * Insert a new file into the database
 */
void insert_file(const gchar *path, const gchar *filename)
{
    int status = 0;

    sqlite3_reset(stmt_insert_file);
    status = sqlite3_bind_text(stmt_insert_file, 1,
            path, -1, SQLITE_STATIC);
    status = sqlite3_bind_text(stmt_insert_file, 2,
            filename, -1, SQLITE_STATIC);
    handle_sql_error(status, __LINE__);

    status = sqlite3_step(stmt_insert_file);
    handle_sql_error(status, __LINE__);
}

/*
 * Insert a new namespace into the database or do nothing if it already exists
 */
gint64 insert_namespace(const gchar* namespace)
{
    int status          = 0;
    gint64 namespace_id = 0;
    gint64 *data        = NULL;

    // check if the namespace was already inserted and return its ID if this is
    // the case
    data = (gint64*) g_hash_table_lookup(inserted_namespaces, namespace);

    if (data != NULL) {
        namespace_id = *data;
        return namespace_id;
    }

    sqlite3_reset(stmt_insert_namespace);
    status = sqlite3_bind_text(stmt_insert_namespace, 1, namespace,
            -1, SQLITE_STATIC);
    handle_sql_error(status, __LINE__);

    status = sqlite3_step(stmt_insert_namespace);
    handle_sql_error(status, __LINE__);

    namespace_id = sqlite3_last_insert_rowid(db);
    data         = g_new(gint64, 1);
    *data        = namespace_id;

    g_hash_table_insert(inserted_namespaces,
            g_string_chunk_insert(strchunk, namespace), data);

    return namespace_id;
}

/*
 * Insert a new class into the database and return its id
 */
gint64 insert_class(const gchar* classname)
{
    int status           = 0;
    gint64 importable_id = 0;
    gint64 *data         = NULL;

    // check if the class was already inserted and return its ID if this is the
    // case
    data = g_hash_table_lookup(inserted_importables, classname);

    if (data != NULL) {
        importable_id = *data;
        return importable_id;
    }

    sqlite3_reset(stmt_insert_class);
    status = sqlite3_bind_text(stmt_insert_class, 1, classname,
            -1, SQLITE_STATIC);
    handle_sql_error(status, __LINE__);

    status = sqlite3_step(stmt_insert_class);
    handle_sql_error(status, __LINE__);

    importable_id = sqlite3_last_insert_rowid(db);
    data          = g_new(gint64, 1);
    *data         = importable_id;

    g_hash_table_insert(inserted_importables,
            g_string_chunk_insert(strchunk, classname), data);

    return importable_id;
}

/*
 * Associate a class with its namespace
 */
gboolean associate_class_and_namespace(gint64 class_id, gint64 namespace_id,
        gboolean done)
{
    int status = 0;
    int done_int = 0;

    if (done) done_int = 1;

    sqlite3_reset(stmt_insert_class_namespace);
    status = sqlite3_bind_int64(stmt_insert_class_namespace, 1, class_id);
    handle_sql_error(status, __LINE__);
    status = sqlite3_bind_int64(stmt_insert_class_namespace, 2, namespace_id);
    handle_sql_error(status, __LINE__);
    status = sqlite3_bind_int64(stmt_insert_class_namespace, 3, done);
    handle_sql_error(status, __LINE__);

    status = sqlite3_step(stmt_insert_class_namespace);
    if (status == SQLITE_CONSTRAINT) {
        if (done == FALSE) return TRUE;

        // check if we have a namespace collision
        sqlite3_reset(stmt_is_done);
        status = sqlite3_bind_int64(stmt_is_done, 1, class_id);
        handle_sql_error(status, __LINE__);
        status = sqlite3_bind_int64(stmt_is_done, 2, namespace_id);
        handle_sql_error(status, __LINE__);

        status = sqlite3_step(stmt_is_done);
        handle_sql_error(status, __LINE__);

        gint64 is_done = sqlite3_column_int(stmt_is_done, 0);

        if (!is_done) {
            // set the done flag
            sqlite3_reset(stmt_set_done);
            status = sqlite3_bind_int64(stmt_set_done, 1, class_id);
            handle_sql_error(status, __LINE__);
            status = sqlite3_bind_int64(stmt_set_done, 2, namespace_id);
            handle_sql_error(status, __LINE__);

            status = sqlite3_step(stmt_set_done);
            handle_sql_error(status, __LINE__);

            return TRUE;
        }

        return FALSE;
    } else {
        handle_sql_error(status, __LINE__);
    }

    return TRUE; // everything is ok
}


void set_class_attributes(JavaClass *c, gint64 class_id, gint64 namespace_id,
        gint64 parent_class_id, gint64 parent_namespace_id)
{
    int status = 0;

    sqlite3_reset(stmt_set_class_attributes);
    status = sqlite3_bind_int64(stmt_set_class_attributes, 1,
            parent_class_id);
    handle_sql_error(status, __LINE__);
    status = sqlite3_bind_int64(stmt_set_class_attributes, 2,
            parent_namespace_id);
    handle_sql_error(status, __LINE__);
    status = sqlite3_bind_int(stmt_set_class_attributes, 3,
            javaclass_is_public(c));
    handle_sql_error(status, __LINE__);
    status = sqlite3_bind_int(stmt_set_class_attributes, 4,
            javaclass_is_final(c));
    handle_sql_error(status, __LINE__);
    status = sqlite3_bind_int(stmt_set_class_attributes, 5,
            javaclass_is_interface(c));
    handle_sql_error(status, __LINE__);
    status = sqlite3_bind_int(stmt_set_class_attributes, 6,
            javaclass_is_abstract(c));
    handle_sql_error(status, __LINE__);
    status = sqlite3_bind_int(stmt_set_class_attributes, 7,
            javaclass_is_annotation(c));
    handle_sql_error(status, __LINE__);
    status = sqlite3_bind_int(stmt_set_class_attributes, 8,
            javaclass_is_enum(c));
    handle_sql_error(status, __LINE__);
    status = sqlite3_bind_text(stmt_set_class_attributes, 9,
            javaclass_get_signature(c), -1, SQLITE_STATIC);
    handle_sql_error(status, __LINE__);
    status = sqlite3_bind_int64(stmt_set_class_attributes, 10, class_id);
    handle_sql_error(status, __LINE__);
    status = sqlite3_bind_int64(stmt_set_class_attributes, 11, namespace_id);
    handle_sql_error(status, __LINE__);

    status = sqlite3_step(stmt_set_class_attributes);
    handle_sql_error(status, __LINE__);
}

/*
 * Insert all fields of a class into the database
 */
void insert_fields(JavaClass *c, gint64 class_id, gint64 namespace_id)
{
    int status = 0;

    if (javaclass_get_field_number(c) <= 0) return;

    JavaField** fields = javaclass_get_fields(c);
    for (int i = 0; fields[i]; i++) {
        sqlite3_reset(stmt_insert_field);
        status = sqlite3_bind_text(stmt_insert_field, 1,
                javafield_get_name(fields[i]), -1, SQLITE_STATIC);
        handle_sql_error(status, __LINE__);
        status = sqlite3_bind_text(stmt_insert_field, 2,
                javafield_get_descriptor(fields[i]), -1, SQLITE_STATIC);
        handle_sql_error(status, __LINE__);
        status = sqlite3_bind_text(stmt_insert_field, 3,
                javafield_get_signature(fields[i]), -1, SQLITE_STATIC);
        handle_sql_error(status, __LINE__);
        status = sqlite3_bind_int64(stmt_insert_field, 4,
                class_id);
        handle_sql_error(status, __LINE__);
        status = sqlite3_bind_int64(stmt_insert_field, 5,
                namespace_id);
        handle_sql_error(status, __LINE__);
        status = sqlite3_bind_int(stmt_insert_field, 6,
                javafield_is_public(fields[i]));
        handle_sql_error(status, __LINE__);
        status = sqlite3_bind_int(stmt_insert_field, 7,
                javafield_is_protected(fields[i]));
        handle_sql_error(status, __LINE__);
        status = sqlite3_bind_int(stmt_insert_field, 8,
                javafield_is_private(fields[i]));
        handle_sql_error(status, __LINE__);
        status = sqlite3_bind_int(stmt_insert_field, 9,
                javafield_is_static(fields[i]));
        handle_sql_error(status, __LINE__);
        status = sqlite3_bind_int(stmt_insert_field, 10,
                javafield_is_final(fields[i]));
        handle_sql_error(status, __LINE__);
        status = sqlite3_bind_int(stmt_insert_field, 11,
                javafield_is_enum(fields[i]));
        handle_sql_error(status, __LINE__);

        status = sqlite3_step(stmt_insert_field);
        handle_sql_error(status, __LINE__);
    }
}

/*
 * Insert all methods of a class into the database
 */
void insert_methods(JavaClass *c, gint64 class_id, gint64 namespace_id)
{
    int status = 0;

    if (javaclass_get_method_number(c) <= 0) return;

    JavaMethod** methods = javaclass_get_methods(c);
    for (int i = 0; methods[i]; i++) {
        sqlite3_reset(stmt_insert_method);
        status = sqlite3_bind_text(stmt_insert_method, 1,
                javamethod_get_name(methods[i]), -1, SQLITE_STATIC);
        handle_sql_error(status, __LINE__);
        status = sqlite3_bind_text(stmt_insert_method, 2,
                javamethod_get_descriptor(methods[i]), -1, SQLITE_STATIC);
        handle_sql_error(status, __LINE__);
        status = sqlite3_bind_text(stmt_insert_method, 3,
                javamethod_get_signature(methods[i]), -1, SQLITE_STATIC);
        handle_sql_error(status, __LINE__);
        status = sqlite3_bind_int64(stmt_insert_method, 4,
                class_id);
        handle_sql_error(status, __LINE__);
        status = sqlite3_bind_int64(stmt_insert_method, 5,
                namespace_id);
        handle_sql_error(status, __LINE__);
        status = sqlite3_bind_int(stmt_insert_method, 6,
                javamethod_is_public(methods[i]));
        handle_sql_error(status, __LINE__);
        status = sqlite3_bind_int(stmt_insert_method, 7,
                javamethod_is_protected(methods[i]));
        handle_sql_error(status, __LINE__);
        status = sqlite3_bind_int(stmt_insert_method, 8,
                javamethod_is_private(methods[i]));
        handle_sql_error(status, __LINE__);
        status = sqlite3_bind_int(stmt_insert_method, 9,
                javamethod_is_static(methods[i]));
        handle_sql_error(status, __LINE__);
        status = sqlite3_bind_int(stmt_insert_method, 10,
                javamethod_is_final(methods[i]));
        handle_sql_error(status, __LINE__);
        status = sqlite3_bind_int(stmt_insert_method, 11,
                javamethod_is_synchronized(methods[i]));
        handle_sql_error(status, __LINE__);
        status = sqlite3_bind_int(stmt_insert_method, 12,
                javamethod_is_abstract(methods[i]));
        handle_sql_error(status, __LINE__);

        status = sqlite3_step(stmt_insert_method);
        handle_sql_error(status, __LINE__);

        gint64 method_id = sqlite3_last_insert_rowid(db);

        // insert exceptions
        gchar **exceptions = javamethod_get_exceptions(methods[i]);
        if (exceptions == NULL) continue;

        for (int i = 0; exceptions[i]; i++) {
            gchar *cur = exceptions[i];
            gchar *classname = javaclass_extract_classname(cur);
            gchar *package = javaclass_extract_package(cur);

            gint64 namespace_id = insert_namespace(package);
            g_free(package);

            gint64 class_id = insert_class(classname);
            g_free(classname);

            associate_class_and_namespace(class_id, namespace_id, FALSE);

            sqlite3_reset(stmt_insert_exception);
            status = sqlite3_bind_int64(stmt_insert_exception, 1,
                    method_id);
            handle_sql_error(status, __LINE__);
            status = sqlite3_bind_int64(stmt_insert_exception, 2,
                    class_id);
            handle_sql_error(status, __LINE__);
            status = sqlite3_bind_int64(stmt_insert_exception, 3,
                    namespace_id);
            handle_sql_error(status, __LINE__);

            status = sqlite3_step(stmt_insert_exception);
            handle_sql_error(status, __LINE__);
        }
    }
}

/*
 * Insert all the interfaces implemented by a class
 */
void insert_interfaces(JavaClass *c, gint64 class_id, gint64 namespace_id)
{
    if (javaclass_get_interface_number(c) <= 0) return;

    int status = 0;
    gchar **interfaces = javaclass_get_interfaces(c);

    for (int i = 0; interfaces[i]; i++) {
        gchar *cur = interfaces[i];
        if (g_strrstr(cur, "$") != NULL) continue; // skip inner interfaces

        gchar *classname = javaclass_extract_classname(cur);
        gchar *package = javaclass_extract_package(cur);

        gint64 interface_namespace_id = insert_namespace(package);
        g_free(package);

        gint64 interface_class_id = insert_class(classname);
        g_free(classname);

        associate_class_and_namespace(interface_class_id,
                interface_namespace_id, FALSE);

        sqlite3_reset(stmt_insert_interface);
        status = sqlite3_bind_int64(stmt_insert_interface, 1,
                class_id);
        handle_sql_error(status, __LINE__);
        status = sqlite3_bind_int64(stmt_insert_interface, 2,
                namespace_id);
        handle_sql_error(status, __LINE__);

        status = sqlite3_bind_int64(stmt_insert_interface, 3,
                interface_class_id);
        handle_sql_error(status, __LINE__);
        status = sqlite3_bind_int64(stmt_insert_interface, 4,
                interface_namespace_id);
        handle_sql_error(status, __LINE__);

        status = sqlite3_step(stmt_insert_interface);
        handle_sql_error(status, __LINE__);
    }
}

/*
 * Takes the bytes of a classfile and analyzes and indexes this class
 */
void process_class(JavaClass *c)
{
    gboolean no_collision = TRUE;
    const gchar *namespace = NULL;

    namespace = javaclass_get_package(c);
    if (namespace == NULL) namespace = DEFAULT_PACKAGE;
    if (strlen(namespace) <= 0) namespace = DEFAULT_PACKAGE;

    gint64 namespace_id = insert_namespace(namespace);
    g_assert(namespace_id != 0);
    gint64 class_id = insert_class(javaclass_get_name(c));
    g_assert(class_id != 0);

    no_collision = associate_class_and_namespace(class_id, namespace_id, TRUE);

    // only add the fields if we don't have a namespace collision
    if (no_collision) {
        gint64 parent_namespace_id = 0;
        gint64 parent_class_id = 0;
        const gchar *parent = javaclass_get_fq_parent(c);

        if (parent != NULL) {
            gchar *parent_package = javaclass_extract_package(parent);
            gchar *parent_class   = javaclass_extract_classname(parent);

            if (parent_package == NULL) parent_package = DEFAULT_PACKAGE;
            parent_namespace_id = insert_namespace(parent_package);
            parent_class_id = insert_class(parent_class);

            if (g_strcmp0(parent_package, DEFAULT_PACKAGE) == 0) g_free(parent_package);
            g_free(parent_class);
        }

        set_class_attributes(c, class_id, namespace_id, parent_class_id,
                parent_namespace_id);
        insert_fields(c, class_id, namespace_id);
        insert_methods(c, class_id, namespace_id);
        insert_interfaces(c, class_id, namespace_id);
    } else {
        fprintf(stderr, "ERROR: Possible namespace collision\n");
        fprintf(stderr, "Class %s is already in the database\n",
                javaclass_get_fq_name(c));
    }

    javaclass_free(c);
}

/*
 * Index all the entries of a CLASSPATH style list of directories and JARs
 */
void index_classpath(gchar *classpath)
{
    gchar **entries = NULL;

    if (classpath == NULL) return;
    if (strlen(classpath) == 0) return;

    entries = g_strsplit(classpath, G_SEARCHPATH_SEPARATOR_S, 0);

    for (int i = 0; entries[i] != NULL; i++) {
        if (g_str_has_suffix(entries[i], ".jar")) {
            index_jar(entries[i]);
        } else {
            if (g_strcmp0(entries[i], ".") == 0) continue;
            index_dir(entries[i], FALSE);
        }
    }

    g_strfreev(entries);
}

/*
 * Create a index database from scratch
 */
void create_database()
{
    int status = 0;
    FILE *fp = NULL;
    gchar *error_msg = NULL;

    // overwrite the DB file if it already exists
    fp = fopen(DB_FILE, "w");
    fclose(fp);

    status = sqlite3_open(DB_FILE, &db);

    if (status != 0) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    // set pragmas
    sqlite3_exec(db, "PRAGMA synchronous = OFF", NULL, 0, NULL);

    // create all the tables by executing the DDL statements
    status = sqlite3_exec(db, DDL, NULL, 0, &error_msg);

    if (status != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", error_msg);
        exit(1);
    }
}

/*
 * Create all indexes we use on the table
 *
 * We create the indexes after we executed all INSERTs since SQLite is faster
 * if the indexes are created once instead of having to update them with
 * each INSERT
 */
void create_indexes()
{
    int status = sqlite3_exec(db, INDEXES, NULL, 0, NULL);
    handle_sql_error(status, __LINE__);
}

void handle_sql_error(int status, int line)
{
    if (status != SQLITE_OK && status != SQLITE_DONE && status != SQLITE_ROW) {
        fprintf(stderr, "SQL error on line %d: %s\n", line, sqlite3_errmsg(db));
        exit(1);
    }
}
