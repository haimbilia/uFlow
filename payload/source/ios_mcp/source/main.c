#include "ipc.h"

static int threadsStarted = 0;

int _startMainThread(void) {
    if (threadsStarted == 0) {
        threadsStarted = 1;

        ipc_init();
    }
    return 0;
}