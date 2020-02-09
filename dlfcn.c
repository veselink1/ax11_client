#include <dlfcn.h>

void dlfcn_require(void)
{
    dlopen(0, 0);
    dlerror();
    dlsym(0, 0);
    dlclose(0);
}