#include "user_store.h"

#include <string.h>

struct user_entry {
    bool used;
    uint8_t username[USER_STORE_MAX_FIELD + 1];
    uint8_t password[USER_STORE_MAX_FIELD + 1];
    size_t username_len;
    size_t password_len;
};

static struct user_entry users[USER_STORE_MAX_USERS] = {
    {
        .used = true,
        .username = "admin",
        .password = "admin",
        .username_len = 5,
        .password_len = 5,
    },
};

static bool
valid_field(const uint8_t *value, const size_t len) {
    return value != NULL && len > 0 && len <= USER_STORE_MAX_FIELD;
}

static bool
same_bytes(const uint8_t *a, const size_t a_len,
           const uint8_t *b, const size_t b_len) {
    return a_len == b_len && memcmp(a, b, a_len) == 0;
}

static struct user_entry *
find_user(const uint8_t *username, const size_t username_len) {
    for(size_t i = 0; i < USER_STORE_MAX_USERS; i++) {
        if(users[i].used
        && same_bytes(users[i].username, users[i].username_len,
                      username, username_len)) {
            return &users[i];
        }
    }
    return NULL;
}

bool
user_store_validate(const uint8_t *username, const size_t username_len,
                    const uint8_t *password, const size_t password_len) {
    if(!valid_field(username, username_len) || !valid_field(password, password_len)) {
        return false;
    }

    struct user_entry *user = find_user(username, username_len);
    return user != NULL
        && same_bytes(user->password, user->password_len, password, password_len);
}

bool
user_store_add(const uint8_t *username, const size_t username_len,
               const uint8_t *password, const size_t password_len) {
    if(!valid_field(username, username_len) || !valid_field(password, password_len)) {
        return false;
    }
    if(find_user(username, username_len) != NULL) {
        return false;
    }

    for(size_t i = 0; i < USER_STORE_MAX_USERS; i++) {
        if(!users[i].used) {
            users[i].used = true;
            memcpy(users[i].username, username, username_len);
            memcpy(users[i].password, password, password_len);
            users[i].username[username_len] = 0;
            users[i].password[password_len] = 0;
            users[i].username_len = username_len;
            users[i].password_len = password_len;
            return true;
        }
    }
    return false;
}

bool
user_store_remove(const uint8_t *username, const size_t username_len) {
    if(!valid_field(username, username_len)) {
        return false;
    }

    struct user_entry *user = find_user(username, username_len);
    if(user == NULL) {
        return false;
    }

    memset(user, 0, sizeof(*user));
    return true;
}

size_t
user_store_count(void) {
    size_t count = 0;
    for(size_t i = 0; i < USER_STORE_MAX_USERS; i++) {
        if(users[i].used) {
            count++;
        }
    }
    return count;
}
