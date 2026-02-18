#!/usr/bin/crispy

#define CRISPY_PARAMS "$(pkg-config --cflags --libs ai-glib-1.0)"

#include <ai-glib.h>
#include <stdio.h>

/*
 * Assumes you have ai-glib installed system wide with pkg-config 
 * (default on Immutablue systems)
 *
 * Reads provider/model from config files and env vars automatically.
 * Tests both one-shot prompt and multi-turn chat.
 *
 * If no env-vars reads config from ~/.config/ai-glib/config.yaml:
 *  default_provider: grok
 *  default_model: grok-4-1-fast-reasoning
 *
 *  # XAI_API_KEY is exposed via env-var
 *  # providers:
 *  #   grok:
 *  #     api_key: YOUR_XAI_API_KEY_HERE
 *
 */

int
main(void)
{
    g_autoptr(AiSimple) ai = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *answer1 = NULL;
    g_autofree gchar *answer2 = NULL;
    g_autofree gchar *answer3 = NULL;

    ai = ai_simple_new();

    /* one-shot prompt (stateless) */
    printf("--- One-shot prompt ---\n");
    answer1 = ai_simple_prompt(ai, "What is 2 + 2? Reply with just the number.", NULL, &error);
    if (error != NULL)
    {
        fprintf(stderr, "Prompt error: %s\n", error->message);
        return 1;
    }
    printf("Response: %s\n\n", answer1);

    /* multi-turn chat (stateful) */
    printf("--- Multi-turn chat ---\n");

    ai_simple_set_system_prompt(ai, "You are a terse unix sysadmin. One sentence max.");

    answer2 = ai_simple_chat(ai, "What's the best filesystem?", NULL, &error);
    if (error != NULL)
    {
        fprintf(stderr, "Chat error: %s\n", error->message);
        return 1;
    }
    printf("Turn 1: %s\n", answer2);

    answer3 = ai_simple_chat(ai, "Why that one over the alternatives?", NULL, &error);
    if (error != NULL)
    {
        fprintf(stderr, "Chat error: %s\n", error->message);
        return 1;
    }
    printf("Turn 2: %s\n", answer3);

    printf("\nDone.\n");
    return 0;
}

