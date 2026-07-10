#ifndef MONITOR_H
#define MONITOR_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "selector.h"


// Handler para aceptar nuevas conexiones en el puerto pasivo de monitoreo
void monitor_passive_accept(struct selector_key *key);


#endif // MONITOR_H
