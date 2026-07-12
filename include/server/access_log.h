#ifndef ACCESS_LOG_H
#define ACCESS_LOG_H

#include <stddef.h>
#include <stdint.h>

#define ACCESS_LOG_MAX_ENTRIES 32
#define ACCESS_LOG_MAX_USERNAME 64
#define ACCESS_LOG_MAX_DESTINATION 255
#define ACCESS_LOG_MAX_STATUS 32
#define ACCESS_LOG_TIMESTAMP_LEN 20

struct access_log_entry {
    char timestamp[ACCESS_LOG_TIMESTAMP_LEN];
    char username[ACCESS_LOG_MAX_USERNAME + 1];
    char destination[ACCESS_LOG_MAX_DESTINATION + 1];
    uint16_t port;
    char status[ACCESS_LOG_MAX_STATUS + 1];
};

void access_log_add(const char *username, const char *destination,
                    uint16_t port, const char *status);

size_t access_log_count(void);

size_t access_log_copy(struct access_log_entry *entries, size_t max_entries);

#endif
