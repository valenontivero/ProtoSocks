#ifndef USER_STORE_H
#define USER_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define USER_STORE_MAX_USERS 10
#define USER_STORE_MAX_FIELD 255

bool user_store_validate(const uint8_t *username, size_t username_len,
                         const uint8_t *password, size_t password_len);

bool user_store_add(const uint8_t *username, size_t username_len,
                    const uint8_t *password, size_t password_len);

bool user_store_remove(const uint8_t *username, size_t username_len);

size_t user_store_count(void);

bool user_store_get_username(size_t index, const uint8_t **username, size_t *username_len);

#endif
