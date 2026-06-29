#include <stdio.h>

#include "shared.h"

int main(void) {
    printf("server %s\n", protosocks_version());
    return 0;
}
