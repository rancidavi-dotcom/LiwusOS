#ifndef LIWUS_USERS_H
#define LIWUS_USERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define USER_NAME_MAX 32
#define USER_PASS_MAX 32
#define USER_HOME_MAX 64
#define MAX_SYSTEM_USERS 16

typedef struct {
    char username[USER_NAME_MAX];
    char password[USER_PASS_MAX];
    uint32_t uid;
    uint32_t gid;
    char home[USER_HOME_MAX];
    char shell[32];
    bool is_admin;
} user_account_t;

/* Inicializacao do subsistema de usuarios */
void users_init(void);

/* Consultas de usuarios */
int users_count(void);
const user_account_t *users_get_at(int index);
const user_account_t *users_get_by_name(const char *username);
const user_account_t *users_get_by_uid(uint32_t uid);

/* Sessao ativa */
const char *users_get_active_name(void);
uint32_t users_get_active_uid(void);
bool users_is_active_elevated(void);
bool users_set_active(const char *username);
bool users_elevate(const char *password);
bool users_unelevate(void);

/* Gestao de usuarios */
bool users_add(const char *username, const char *password, bool is_admin);
bool users_del(const char *username);
bool users_set_password(const char *username, const char *new_password);
bool users_authenticate(const char *username, const char *password);

#endif /* LIWUS_USERS_H */
