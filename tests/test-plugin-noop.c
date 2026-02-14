/* test-plugin-noop.c - Minimal test plugin that does nothing */

#define CRISPY_COMPILATION
#include "crispy-plugin.h"

CRISPY_PLUGIN_DEFINE("test-noop", "Does nothing", "0.1.0", "Test", "AGPLv3");
