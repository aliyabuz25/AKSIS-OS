/* Run the installed UTM QEMU library headlessly for kernel smoke tests.
 * No UTM VM configuration, disks or application files are modified. */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    const char *path = getenv("UTM_QEMU_LIBRARY");
    if (!path) path = "/Applications/UTM.app/Contents/Frameworks/"
        "qemu-x86_64-softmmu.framework/Versions/A/qemu-x86_64-softmmu";
    void *lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!lib) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    void (*init)(int, char **) = dlsym(lib, "qemu_init");
    int (*loop)(void) = dlsym(lib, "qemu_main_loop");
    void (*cleanup)(void) = dlsym(lib, "qemu_cleanup");
    if (!init || !loop || !cleanup) {
        fprintf(stderr, "Missing QEMU entry points\n"); return 1;
    }
    init(argc, argv);
    int result = loop();
    cleanup();
    return result;
}
