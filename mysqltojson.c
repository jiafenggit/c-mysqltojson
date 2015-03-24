#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// These are needed so MySQL won't warn with C99 standard
typedef unsigned long int ulong;
typedef unsigned short int ushort;
typedef unsigned int uint;

#include <mysql/my_global.h>
#include <mysql.h>

#include <fcgi_stdio.h>

#define SMART_STR_USE_REALLOC   // Define this to not use zend perealloc
#include "php_smart_str.h"

// Ugly hack to silent Splint when it doesn't follow multi-dimensional pointers
#ifdef S_SPLINT_S
extern void assertSet(/*@special@*/ /*@sef@*/ /*@unused@*/ void *p_x) /*@sets p_x, *p_x@*/ ;

#else
# define assertSet(x) ;
#endif

static void close_mysql_with_error(MYSQL *connection)
{
    const char* error_message = mysql_error(connection);

    /*@-bounds@*/ (void) FCGI_fprintf(FCGI_stderr, "%s\n", error_message); /*@+bounds@*/

    //fprintf(stderr, "%s\n", mysql_error(connection));    // Short version, will not be accepted by Splint
    free((void *) error_message);
    mysql_close(connection);
}

/**
 * Get array of strings of fields from result, like ["id", "title", ...]
 *
 * @param MYSQL_RES result
 */
static /*@only@*/ /*@null@*/ smart_str** get_field_names(/*@notnull@*/ MYSQL_RES *my_result, int num_fields)
    /*@ensures maxRead(result) == num_fields @*/
{


    smart_str** fields;   // Array of pointers
    MYSQL_FIELD *field = NULL;
    int i;

    // Allocate size of array
    fields = malloc(num_fields * sizeof(smart_str*));

    if (fields == NULL)
    {
        return NULL;
    }

    for (i = 0; i < num_fields; i++)
    {
        field = mysql_fetch_field(my_result);

        if (field == NULL || field->name == NULL) {
            // TODO: Free fields[]
            free(fields);
            return NULL;
        }

        fields[i] = calloc(1, sizeof(smart_str));


        if (fields[i] == NULL) {
            // TODO: Free fields[]
            free(fields);
            return NULL;
        }
        else
        {
            smart_str_appendl(fields[i], field->name, strlen(field->name));
        }
    }

    assertSet(fields);

    return fields;

}

/*@-unqualifiedtrans@*/
static void free_field_names(/*@notnull@*/ /*@special@*/ /*@only@*/ smart_str** strings, int size)
    /*@requires maxRead(strings) >= size @*/
    /*@releases strings@*/
{

    int i;
    for (i = 0; i < size; i++)
    {
        //smart_str_free(strings[i]);
        free(strings[i]->c);
        free(strings[i]);
    }

    free(strings);

}
/*@+unqualifiedtrans@*/

/**
 * Calculate string length of JSON from a MySQL result
 *
 * @param result Result from a MySQL query
 * @param field_names array of strings
 */
/*
   static int json_strlen(MYSQL_RES* result, char** field_names)
   {
   int length = 0;
   int i = 0;
   int num_fields = (int) mysql_num_fields(result);
   MYSQL_ROW row;

   length++; // [

   while ((row = mysql_fetch_row(result, num_fields)))
   {
   length++; // {
   for (i = 0; i < num_fields; i++)
   {
   length++; // "
   }
   }

   return length;
   }
   */

/**
 * Takes a result from a MySQL query and encode it as a JSON string.
 * Returns null if no result is found.
 *
 * @param MYSQL_RES result
 * @return char*
 */
static void result_to_json(MYSQL_RES *result, smart_str* json)
{

    MYSQL_ROW row;

    int i;
    int num_fields = (int) mysql_num_fields(result);
    smart_str** fields = get_field_names(result, num_fields);

    if (fields == NULL)
    {
        return;
    }

    smart_str_appendc(json, '[');

    while ((row = mysql_fetch_row(result)))
    {
        smart_str_appendl(json, "{", 1);

        for (i = 0; i < num_fields; i++)
        {
            // key
            smart_str_appendl(json, "\"", 1);
            smart_str_appendl(json, fields[i]->c, fields[i]->len);
            smart_str_appendl(json, "\": ", 3);

            if (row[i] == NULL)
            {
                smart_str_appendl(json, "null", 4);
                smart_str_appendl(json, ", ", 2);
            }
            else
            {
                smart_str_appendl(json, "\"", 1);
                smart_str_appendl(json, row[i], strlen(row[i]));
                smart_str_appendl(json, "\", ", 3);
            }

        }

        if (json == NULL) {
            free_field_names(fields, num_fields);
            return;
        }

        // Strip last ','
        json->len--;
        json->len--;

        smart_str_appendl(json, "}, ", 3);
    }

    if (json == NULL)
    {
        free_field_names(fields, num_fields);
        return;
    }

    // Strip last ','
    json->len--;
    json->len--;

    smart_str_appendl(json, "]", 1);
    smart_str_0(json);

    free_field_names(fields, num_fields);

    return;
}

/**
 * Test FastCGI with C
 *
 * Compile with:
 *  gcc -o tiny.fcgi tiny-cgi.c -lfcgi -lmysqlclient -I/usr/include/mysql -std=c99
 *
 * Copy to /var/www/cgi
 *
 * Use splint to check for errors:
 * splint tiny-cgi.c  -I/usr/include/glib-2.0 -I/usr/lib/x86_64-linux-gnu/glib-2.0/include -I /usr/include/x86_64-linux-gnu/ -I/usr/include/mysql +posixlib
 *
 * The annotation @observer@ means that returned data from function is read-only. Needed for e.g. string literals
 *
 * Or you could use clang's scan-build:
 * scan-build make
 *
 * Also analyze memory with valgrind
 *
 * @since 2014-08-05
 * @author Olle Haerstedt <o.haerstedt@bitmotion.de>
 */
int main(void)
{
    //char* s = getenv("SERVER_NAME");    // This returns null
    MYSQL*  connection = NULL;
    MYSQL_RES* result = NULL;
    size_t newlen;
    smart_str json = {0, 0, 0};
    smart_str_alloc(&json, 1024 * 20, 0);

    /*
    // Test smart_str
    int i;
    smart_str* sstr;
    sstr = malloc(sizeof(smart_str));
    smart_str_appendl(sstr, "null", 4);
    smart_str_0(sstr);

    // Test smart_str array
    smart_str** arr;
    arr = malloc(10 * sizeof(smart_str*));
    for (i = 0; i < 10; i++)
    {
    arr[i] = malloc(sizeof(smart_str));
    if (arr[i] == NULL)
    {
    continue;
    }
    else
    {
    smart_str_append_long(arr[i], i);
    smart_str_0(sstr);
    }
    }
    */

    // Main loop of CGI script. Each connection will begin here.
    while(FCGI_Accept() >= 0)
    {
        // Test smart_str
        /*
           FCGI_printf("%s\n", sstr->c);
           for (i = 0; i < 10; i++)
           {
           FCGI_printf("%s\n", arr[i]->c);
           }
           continue;
           */

        // Header needed
        (void) FCGI_printf("Content-type: text/html\r\n\r\n");

        //FCGI_printf("%s\n", sstr->c);
        //json = malloc(sizeof(smart_str));

        //if (json == NULL)
        //{
        //(void) FCGI_printf("Error: Could not allocate memory for smart_str");
        //continue;
        //}

        connection = mysql_init(NULL);

        if (connection == NULL)
        {
            /*@-bounds@*/ (void) FCGI_fprintf(FCGI_stderr, "Could not connect to MySQL: %s\n", mysql_error(connection)); /*@+bounds@*/
            continue;
        }

        // Connect to database
        if (mysql_real_connect(connection, "localhost", "root", "noten", "smart_dev", 0, NULL, 0) == NULL)
        {
            close_mysql_with_error(connection);
            continue;
        }

        // Select from pages
        if (mysql_query(connection, "SELECT * FROM pages") != 0)
        {
            close_mysql_with_error(connection);
            continue;
        }

        // Get result
        result = mysql_store_result(connection);

        // Abort if no result
        if (result == NULL)
        {
            close_mysql_with_error(connection);
            continue;
        }

        result_to_json(result, &json);
        if (json.c != NULL)
            (void) FCGI_printf("json = %s\n", json.c);
        smart_str_free(&json);

        if (result != NULL) {
            mysql_free_result(result);
        }

        if (connection != NULL) {
            mysql_close(connection);
        }

    }

    mysql_library_end();

    FCGI_Finish();

    smart_str_free(&json);

    return 0;
}
