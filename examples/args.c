#!/usr/bin/crispy

/* args.c - Argument passing demonstration */

#include <glib.h>

gint
main(
    gint    argc,
    gchar **argv
){
    gint i;

    g_print("argc = %d\n", argc);

    for (i = 0; i < argc; i++)
    {
        g_print("  argv[%d] = \"%s\"\n", i, argv[i]);
    }

    if (argc < 2)
    {
        g_print("\nUsage: crispy args.c <arg1> [arg2] ...\n");
        g_print("  Pass some arguments to see them printed!\n");
    }

    return 0;
}
