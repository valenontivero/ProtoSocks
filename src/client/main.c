#include <stdio.h>

#include "shared.h"

int main(void) {
    printf("client %s\n", protosocks_version());
    return 0;
}
