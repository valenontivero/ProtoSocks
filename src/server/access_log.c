#include "access_log.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static struct access_log_entry entries[ACCESS_LOG_MAX_ENTRIES];
static size_t next_entry = 0;
static size_t entry_count = 0;

static void copy_field(char *dst, size_t dst_size, const char *src) {
    if(dst_size == 0) {
        return;
    }
    if(src == NULL || *src == '\0') {
        src = "-";
    }
    snprintf(dst, dst_size, "%s", src);
}

void access_log_add(const char *username, const char *destination,
                    const uint16_t port, const char *status) {
    struct access_log_entry *entry = &entries[next_entry];
    time_t now = time(NULL);
    struct tm tm_now;

    memset(entry, 0, sizeof(*entry));
    if(localtime_r(&now, &tm_now) != NULL) {
        strftime(entry->timestamp, sizeof(entry->timestamp),
                 "%Y-%m-%d %H:%M:%S", &tm_now);
    } else {
        copy_field(entry->timestamp, sizeof(entry->timestamp), "unknown-time");
    }

    copy_field(entry->username, sizeof(entry->username), username);
    copy_field(entry->destination, sizeof(entry->destination), destination);
    entry->port = port;
    copy_field(entry->status, sizeof(entry->status), status);

    next_entry = (next_entry + 1) % ACCESS_LOG_MAX_ENTRIES;
    if(entry_count < ACCESS_LOG_MAX_ENTRIES) {
        entry_count++;
    }
}

size_t access_log_count(void) {
    return entry_count;
}

size_t access_log_copy(struct access_log_entry *out, const size_t max_entries) {
    if(out == NULL || max_entries == 0) {
        return 0;
    }

    const size_t count = entry_count < max_entries ? entry_count : max_entries;
    const size_t first = entry_count == ACCESS_LOG_MAX_ENTRIES ? next_entry : 0;

    for(size_t i = 0; i < count; i++) {
        const size_t idx = (first + i) % ACCESS_LOG_MAX_ENTRIES;
        out[i] = entries[idx];
    }
    return count;
}
