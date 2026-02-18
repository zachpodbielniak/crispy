#!/usr/bin/crispy

/*
 * physics.c - Projectile motion calculator
 *
 * NOTE: This script requires -lm but does NOT define CRISPY_PARAMS.
 * It relies on the config file providing -lm via extra_flags.
 *
 * Run with the example config:
 *   crispy -c data/example-config.c examples/physics.c
 *
 * Without config, this will fail to compile (undefined reference to
 * sin/cos/sqrt). Compare with examples/math.c which self-declares
 * its own CRISPY_PARAMS "-lm".
 */

#include <math.h>
#include <glib.h>

/*
 * simulate_projectile:
 * @v0: initial velocity (m/s)
 * @angle_deg: launch angle (degrees)
 * @dt: time step (seconds)
 *
 * Simulates projectile motion under standard gravity (9.81 m/s^2)
 * and prints the trajectory. Stops when the projectile hits the
 * ground (y <= 0 after launch).
 */
static void
simulate_projectile(
    gdouble v0,
    gdouble angle_deg,
    gdouble dt
){
    gdouble angle_rad;
    gdouble vx;
    gdouble vy;
    gdouble x;
    gdouble y;
    gdouble t;
    gdouble max_height;
    const gdouble g = 9.81;

    angle_rad = angle_deg * G_PI / 180.0;
    vx = v0 * cos(angle_rad);
    vy = v0 * sin(angle_rad);
    x = 0.0;
    y = 0.0;
    t = 0.0;
    max_height = 0.0;

    g_print("  Launch: v0=%.1f m/s, angle=%.1f deg\n", v0, angle_deg);
    g_print("  %8s  %10s  %10s\n", "time(s)", "x(m)", "y(m)");
    g_print("  %8s  %10s  %10s\n", "-------", "------", "------");

    while (TRUE)
    {
        g_print("  %8.2f  %10.2f  %10.2f\n", t, x, y);

        if (y > max_height)
            max_height = y;

        /* advance one time step */
        t += dt;
        x = vx * t;
        y = vy * t - 0.5 * g * t * t;

        /* stop when projectile hits the ground (after launch) */
        if (t > dt && y <= 0.0)
            break;
    }

    g_print("\n  Range:      %.2f m\n", x);
    g_print("  Max height: %.2f m\n", max_height);
    g_print("  Flight time: %.2f s\n", t);
}

gint
main(
    gint    argc,
    gchar **argv
){
    gdouble velocity;
    gdouble angle;

    velocity = 50.0;
    angle = 45.0;

    if (argc >= 2)
        velocity = g_ascii_strtod(argv[1], NULL);
    if (argc >= 3)
        angle = g_ascii_strtod(argv[2], NULL);

    g_print("Projectile Motion Simulator\n");
    g_print("(requires -lm from config extra_flags)\n\n");

    simulate_projectile(velocity, angle, 0.5);

    return 0;
}
