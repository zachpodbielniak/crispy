/* crispy.h - Umbrella header for Crispy */

#ifndef CRISPY_H
#define CRISPY_H

/**
 * SECTION:crispy
 * @title: Crispy
 * @short_description: GLib-native C scripting
 *
 * Crispy (Crispy Really Is Super Powerful Yo) is a library and tool
 * for running C source files as scripts. Scripts are compiled to
 * shared objects on-the-fly using gcc, cached by content hash, and
 * executed via GModule.
 *
 * To use the library, include only this header:
 * |[<!-- language="C" -->
 * #include <crispy.h>
 * ]|
 */

#define CRISPY_INSIDE

#include "crispy-types.h"
#include "crispy-version.h"
#include "crispy-plugin.h"
#include "interfaces/crispy-compiler.h"
#include "interfaces/crispy-cache-provider.h"
#include "core/crispy-gcc-compiler.h"
#include "core/crispy-file-cache.h"
#include "core/crispy-plugin-engine.h"
#include "core/crispy-script.h"
#include "core/crispy-config-context.h"

#undef CRISPY_INSIDE

#endif /* CRISPY_H */
