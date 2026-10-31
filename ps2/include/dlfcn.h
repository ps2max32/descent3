/* dlfcn.h for the PS2: no dynamic loading; the modules (level scripts) are
   linked in, and dlopen finds them by their file name (ps2/ps2_dl.cpp). */
#ifndef PS2_DLFCN_H
#define PS2_DLFCN_H

#define RTLD_LAZY 1
#define RTLD_NOW 2
#define RTLD_GLOBAL 0x100
#define RTLD_LOCAL 0

#ifdef __cplusplus
extern "C" {
#endif
void *dlopen(const char *file, int mode);
void *dlsym(void *handle, const char *name);
int dlclose(void *handle);
char *dlerror(void);
#ifdef __cplusplus
}
#endif
#endif
