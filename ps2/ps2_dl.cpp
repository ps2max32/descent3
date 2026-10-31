/* ps2_dl.cpp: dlopen and friends (dlfcn.h of the port) on the modules that
   are linked in: the level scripts (OSIRIS), each with its exports renamed
   <module>_<export> (Makefile). dlopen takes the module's file name
   ("level1.so", in any directory, any case), dlsym the export's name. */

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "ps2_modules.h"

static char err[160];

extern "C" void *dlopen(const char *file, int mode) {
  if (!file) {
    snprintf(err, sizeof err, "no main program module");
    return NULL;
  }
  const char *base = strrchr(file, '/');
  base = base ? base + 1 : file;
  size_t n = strcspn(base, ".");
  for (const ps2_module *m = ps2_modules; m->name; m++)
    if (strlen(m->name) == n && !strncasecmp(m->name, base, n))
      return (void *)m;
  snprintf(err, sizeof err, "no module %s linked in", base);
  return NULL;
}

extern "C" void *dlsym(void *handle, const char *name) {
  const ps2_module *m = (const ps2_module *)handle;
  for (const ps2_export *e = m->exports; e->name; e++)
    if (!strcmp(e->name, name))
      return e->fn;
  snprintf(err, sizeof err, "module %s has no %s", m->name, name);
  return NULL;
}

extern "C" int dlclose(void *handle) { return 0; }

extern "C" char *dlerror(void) { return err[0] ? err : NULL; }
