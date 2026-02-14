#!/usr/bin/crispy

/* math.c - CRISPY_PARAMS demo with -lm */

#define CRISPY_PARAMS "-lm"

#include <math.h>
#include <glib.h>

gint
main(
    gint    argc,
    gchar **argv
){
    gdouble val;
    gdouble angle;
    gint i;

    g_print("CRISPY_PARAMS demo: linking against libm\n\n");

    /* square roots */
    for (i = 1; i <= 10; i++)
    {
        val = sqrt((gdouble)i);
        g_print("  sqrt(%2d) = %.6f\n", i, val);
    }

    /* trig */
    g_print("\nTrigonometry:\n");
    angle = G_PI / 4.0;
    g_print("  sin(pi/4) = %.6f\n", sin(angle));
    g_print("  cos(pi/4) = %.6f\n", cos(angle));
    g_print("  tan(pi/4) = %.6f\n", tan(angle));

    /* powers */
    g_print("\nPowers:\n");
    g_print("  2^10 = %.0f\n", pow(2.0, 10.0));
    g_print("  e^1  = %.6f\n", exp(1.0));
    g_print("  ln(e) = %.6f\n", log(M_E));

    return 0;
}
