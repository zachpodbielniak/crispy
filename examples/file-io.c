#!/usr/bin/crispy

/* file-io.c - GIO file operations example */

#include <glib.h>
#include <gio/gio.h>

gint
main(
    gint    argc,
    gchar **argv
){
    g_autoptr(GFile) file = NULL;
    g_autoptr(GFileOutputStream) out = NULL;
    g_autoptr(GFileInputStream) in = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *contents = NULL;
    const gchar *message;
    const gchar *path;
    gsize bytes_written;
    gsize length;

    path = "/tmp/crispy-file-io-demo.txt";
    message = "Hello from crispy GIO!\n";

    /* write to a file using GIO */
    file = g_file_new_for_path(path);
    out = g_file_replace(file, NULL, FALSE,
                         G_FILE_CREATE_REPLACE_DESTINATION,
                         NULL, &error);
    if (out == NULL)
    {
        g_printerr("Write error: %s\n", error->message);
        return 1;
    }

    if (!g_output_stream_write_all(G_OUTPUT_STREAM(out),
                                    message, strlen(message),
                                    &bytes_written, NULL, &error))
    {
        g_printerr("Write error: %s\n", error->message);
        return 1;
    }
    g_output_stream_close(G_OUTPUT_STREAM(out), NULL, NULL);
    g_print("Wrote %lu bytes to %s\n", (gulong)bytes_written, path);

    /* read it back */
    if (!g_file_get_contents(path, &contents, &length, &error))
    {
        g_printerr("Read error: %s\n", error->message);
        return 1;
    }
    g_print("Read back: %s", contents);

    /* clean up the temp file */
    g_file_delete(file, NULL, NULL);
    g_print("Cleaned up %s\n", path);

    return 0;
}
