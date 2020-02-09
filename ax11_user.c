#include "ax11.h"
#include <stdio.h>
#include <dlfcn.h>

int main()
{
    void* handle = dlopen("/home/veselin/src/ax11_client/libax11.so", RTLD_LAZY);
    if (!handle) {
        printf("dlopen: %s", dlerror());
        return 1;
    }

    int32_t(*fp_connect)(const char* display, void** out_connection);
    *(void**)(&fp_connect) =  dlsym(handle, "ax11_connect");

    void* con;
    if (fp_connect(":1", &con))
        printf("Failed to connect!\n");

    printf("Connected!\n");
}