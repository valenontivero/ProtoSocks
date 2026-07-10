#include <stdint.h>
#include "metrics.h"

static uint64_t historic_connections = 0;
static uint32_t concurrent_connections = 0;
static uint64_t bytes_transferred = 0;

void metrics_init(void) {
    historic_connections = 0;
    concurrent_connections = 0;
    bytes_transferred = 0;
}

void metrics_register_new_connection(void) {
    historic_connections++;
    concurrent_connections++;
}

void metrics_register_closed_connection(void) {
    if (concurrent_connections > 0) {
        concurrent_connections--;
    }
}

void metrics_register_bytes_transferred(uint64_t bytes) {
    bytes_transferred += bytes;
}

uint64_t get_metrics_historic_connections(void) {
    return historic_connections;
}

uint32_t get_metrics_concurrent_connections(void) {
    return concurrent_connections;
}

uint64_t get_metrics_bytes_transferred(void) {
    return bytes_transferred;
}
