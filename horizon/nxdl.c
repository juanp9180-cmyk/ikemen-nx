// Nintendo Switch: dlopen/dlsym emulation for OpenGL loaders. Lives in its
// own object so that programs without EGL do not pull it in.
#include <EGL/egl.h>
#include <stddef.h>

static char handle;

void *dlopen(const char *file, int mode) {
	(void)file; (void)mode;
	return &handle;
}

void *dlsym(void *h, const char *name) {
	(void)h;
	return (void*)eglGetProcAddress(name);
}

int dlclose(void *h) {
	(void)h;
	return 0;
}

char *dlerror(void) {
	return NULL;
}
