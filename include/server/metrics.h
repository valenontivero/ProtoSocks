#ifndef METRICS_H
#define METRICS_H

#include <stdint.h>

void metrics_init(void);

void metrics_register_new_connection(void);
void metrics_register_closed_connection(void);
void metrics_register_bytes_transferred(uint64_t bytes);

uint64_t get_metrics_historic_connections(void);
uint32_t get_metrics_concurrent_connections(void);
uint64_t get_metrics_bytes_transferred(void);

#endif // METRICS_H
