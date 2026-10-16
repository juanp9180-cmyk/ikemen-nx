// Nintendo Switch: minimal dlfcn.h. There are no shared libraries; dlsym
// resolves OpenGL entry points through eglGetProcAddress (nxgo/nxdl.c).
#ifndef NX_DLFCN_H
#define NX_DLFCN_H
#define RTLD_LAZY 1
#define RTLD_NOW 2
#define RTLD_GLOBAL 0x100
#define RTLD_DEFAULT ((void*)0)
void *dlopen(const char *file, int mode);
void *dlsym(void *handle, const char *name);
int dlclose(void *handle);
char *dlerror(void);
#endif
