#include <stdio.h>
#include "core.h"

int main(void) {
    struct core_info info;
    int err = core_get_info(&info);
    if (err != 0) {
        fprintf(stderr, "core_get_info: error %d\n", err);
        return 1;
    }
    printf("pid=%lld cpus=%d page_size=%d host=%s\n",
           (long long)info.pid, info.cpus, info.page_size, info.host);
    return 0;
}