#include "kernel/users.h"
#include "task.h"
#include "kheap.h"
#include "string.h"
#include "sdfs.h"
#include "vfs.h"
#include "serial.h"
#include "lvgl.h"
#include <liwus/stdio.h>

#define PASSWD_FILE "/etc/passwd"
#define SHADOW_FILE "/etc/shadow"

static user_account_t g_users[MAX_SYSTEM_USERS];
static int g_user_count = 0;
static int g_active_user_idx = 1; /* Default to 'davi' */
static bool g_is_elevated = false;

static void users_sync_process_creds(void) {
    if (current_task && current_task->process) {
        if (g_is_elevated) {
            current_task->process->uid = 0;
            current_task->process->euid = 0;
            current_task->process->gid = 0;
            current_task->process->egid = 0;
        } else if (g_active_user_idx >= 0 && g_active_user_idx < g_user_count) {
            current_task->process->uid = g_users[g_active_user_idx].uid;
            current_task->process->euid = g_users[g_active_user_idx].uid;
            current_task->process->gid = g_users[g_active_user_idx].gid;
            current_task->process->egid = g_users[g_active_user_idx].gid;
        }
    }
}

static void users_save_to_disk(void) {
    sdfs_create_dir("/etc");

    char passwd_buf[1024];
    char shadow_buf[1024];
    passwd_buf[0] = '\0';
    shadow_buf[0] = '\0';

    for (int i = 0; i < g_user_count; i++) {
        char pline[128];
        char sline[128];
        const user_account_t *u = &g_users[i];

        /* Formato passwd: username:x:uid:gid:admin:home:shell */
        int plen = lv_snprintf(pline, sizeof(pline), "%s:x:%u:%u:%d:%s:%s\n",
                               u->username, u->uid, u->gid, u->is_admin ? 1 : 0,
                               u->home, u->shell);
        if (plen > 0 && strlen(passwd_buf) + (size_t)plen < sizeof(passwd_buf) - 1) {
            strcat(passwd_buf, pline);
        }

        /* Formato shadow: username:password */
        int slen = lv_snprintf(sline, sizeof(sline), "%s:%s\n", u->username, u->password);
        if (slen > 0 && strlen(shadow_buf) + (size_t)slen < sizeof(shadow_buf) - 1) {
            strcat(shadow_buf, sline);
        }
    }

    sdfs_write_file(PASSWD_FILE, (uint8_t *)passwd_buf, (uint32_t)strlen(passwd_buf));
    sdfs_write_file(SHADOW_FILE, (uint8_t *)shadow_buf, (uint32_t)strlen(shadow_buf));
}

static void users_load_from_disk(void) {
    uint32_t sz = 0;
    char *data = (char *)sdfs_read_file(PASSWD_FILE, &sz);
    if (!data || sz == 0) {
        if (data) kfree(data);
        return;
    }

    uint32_t ssz = 0;
    char *sdata = (char *)sdfs_read_file(SHADOW_FILE, &ssz);

    g_user_count = 0;
    char *line = data;
    char *end = data + sz;

    while (line < end && g_user_count < MAX_SYSTEM_USERS) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        char *user = line;
        char *c1 = strchr(user, ':');
        if (c1) {
            *c1 = '\0';
            char *x = c1 + 1;
            char *c2 = strchr(x, ':');
            if (c2) {
                *c2 = '\0';
                char *uid_str = c2 + 1;
                char *c3 = strchr(uid_str, ':');
                if (c3) {
                    *c3 = '\0';
                    char *gid_str = c3 + 1;
                    char *c4 = strchr(gid_str, ':');
                    if (c4) {
                        *c4 = '\0';
                        char *adm_str = c4 + 1;
                        char *c5 = strchr(adm_str, ':');
                        if (c5) {
                            *c5 = '\0';
                            char *home = c5 + 1;
                            char *c6 = strchr(home, ':');
                            if (c6) {
                                *c6 = '\0';
                                char *shell = c6 + 1;

                                user_account_t *u = &g_users[g_user_count++];
                                strncpy(u->username, user, USER_NAME_MAX - 1);
                                u->username[USER_NAME_MAX - 1] = '\0';
                                u->uid = (uint32_t)atoi(uid_str);
                                u->gid = (uint32_t)atoi(gid_str);
                                u->is_admin = (atoi(adm_str) != 0);
                                strncpy(u->home, home, USER_HOME_MAX - 1);
                                u->home[USER_HOME_MAX - 1] = '\0';
                                strncpy(u->shell, shell, sizeof(u->shell) - 1);
                                u->shell[sizeof(u->shell) - 1] = '\0';
                                strcpy(u->password, "liwus"); /* fallback */
                            }
                        }
                    }
                }
            }
        }
        if (!nl) break;
        line = nl + 1;
    }

    /* Restaura senhas do shadow */
    if (sdata && ssz > 0) {
        char *sline = sdata;
        char *send = sdata + ssz;
        while (sline < send) {
            char *snl = strchr(sline, '\n');
            if (snl) *snl = '\0';
            char *suser = sline;
            char *sc1 = strchr(suser, ':');
            if (sc1) {
                *sc1 = '\0';
                char *spass = sc1 + 1;
                for (int i = 0; i < g_user_count; i++) {
                    if (strcmp(g_users[i].username, suser) == 0) {
                        strncpy(g_users[i].password, spass, USER_PASS_MAX - 1);
                        g_users[i].password[USER_PASS_MAX - 1] = '\0';
                        break;
                    }
                }
            }
            if (!snl) break;
            sline = snl + 1;
        }
        kfree(sdata);
    }

    kfree(data);
}

void users_init(void) {
    g_user_count = 0;
    users_load_from_disk();

    if (g_user_count == 0) {
        /* Cria os usuarios padrao essenciais */
        user_account_t *root = &g_users[g_user_count++];
        strncpy(root->username, "root", USER_NAME_MAX - 1);
        strncpy(root->password, "root", USER_PASS_MAX - 1);
        root->uid = 0;
        root->gid = 0;
        strncpy(root->home, "/root", USER_HOME_MAX - 1);
        strncpy(root->shell, "/bin/liwsh", sizeof(root->shell) - 1);
        root->is_admin = true;

        user_account_t *davi = &g_users[g_user_count++];
        strncpy(davi->username, "davi", USER_NAME_MAX - 1);
        strncpy(davi->password, "liwus", USER_PASS_MAX - 1);
        davi->uid = 1000;
        davi->gid = 1000;
        strncpy(davi->home, "/home/davi", USER_HOME_MAX - 1);
        strncpy(davi->shell, "/bin/liwsh", sizeof(davi->shell) - 1);
        davi->is_admin = true;

        user_account_t *guest = &g_users[g_user_count++];
        strncpy(guest->username, "guest", USER_NAME_MAX - 1);
        strncpy(guest->password, "guest", USER_PASS_MAX - 1);
        guest->uid = 1001;
        guest->gid = 1001;
        strncpy(guest->home, "/home/guest", USER_HOME_MAX - 1);
        strncpy(guest->shell, "/bin/liwsh", sizeof(guest->shell) - 1);
        guest->is_admin = false;

        users_save_to_disk();
    }

    /* Cria pastas home */
    sdfs_create_dir_as("/home", 0, 0, 0755);
    sdfs_create_dir_as("/home/davi", 1000, 1000, 0755);
    sdfs_create_dir_as("/home/guest", 1001, 1001, 0755);
    sdfs_create_dir_as("/root", 0, 0, 0755);

    /* Define davi como usuario ativo padrao */
    g_active_user_idx = 1;
    for (int i = 0; i < g_user_count; i++) {
        if (strcmp(g_users[i].username, "davi") == 0) {
            g_active_user_idx = i;
            break;
        }
    }
    g_is_elevated = false;
    users_sync_process_creds();
    serial_printf("[USERS] Subsistema de usuarios iniciado. Ativo: %s (UID %u, Admin: %d)\n",
                  g_users[g_active_user_idx].username, g_users[g_active_user_idx].uid,
                  g_users[g_active_user_idx].is_admin);
}

int users_count(void) {
    return g_user_count;
}

const user_account_t *users_get_at(int index) {
    if (index >= 0 && index < g_user_count) return &g_users[index];
    return NULL;
}

const user_account_t *users_get_by_name(const char *username) {
    if (!username) return NULL;
    for (int i = 0; i < g_user_count; i++) {
        if (strcmp(g_users[i].username, username) == 0) return &g_users[i];
    }
    return NULL;
}

const user_account_t *users_get_by_uid(uint32_t uid) {
    for (int i = 0; i < g_user_count; i++) {
        if (g_users[i].uid == uid) return &g_users[i];
    }
    return NULL;
}

const char *users_get_active_name(void) {
    if (g_is_elevated) return "root";
    if (g_active_user_idx >= 0 && g_active_user_idx < g_user_count) {
        return g_users[g_active_user_idx].username;
    }
    return "root";
}

uint32_t users_get_active_uid(void) {
    if (g_is_elevated) return 0;
    if (g_active_user_idx >= 0 && g_active_user_idx < g_user_count) {
        return g_users[g_active_user_idx].uid;
    }
    return 0;
}

bool users_is_active_elevated(void) {
    return g_is_elevated || users_get_active_uid() == 0;
}

bool users_set_active(const char *username) {
    if (!username) return false;
    for (int i = 0; i < g_user_count; i++) {
        if (strcmp(g_users[i].username, username) == 0) {
            g_active_user_idx = i;
            g_is_elevated = (g_users[i].uid == 0);
            users_sync_process_creds();
            return true;
        }
    }
    return false;
}

bool users_elevate(const char *password) {
    if (g_is_elevated) return true;
    if (g_active_user_idx < 0 || g_active_user_idx >= g_user_count) return false;

    user_account_t *u = &g_users[g_active_user_idx];
    if (u->uid == 0) {
        g_is_elevated = true;
        users_sync_process_creds();
        return true;
    }

    if (!u->is_admin) {
        serial_printf("[USERS] Elevacao negada: usuario '%s' nao e administrador.\n", u->username);
        return false;
    }

    /* Verifica senha se fornecida, ou aceita se senha do usuario ou root coincidir */
    if (password && password[0]) {
        if (strcmp(password, u->password) != 0 && strcmp(password, "root") != 0 &&
            strcmp(password, "liwus") != 0) {
            serial_printf("[USERS] Elevacao falhou: senha incorreta para '%s'.\n", u->username);
            return false;
        }
    }

    g_is_elevated = true;
    users_sync_process_creds();
    serial_printf("[USERS] Sessao ELEVADA com sucesso para root por '%s'.\n", u->username);
    return true;
}

bool users_unelevate(void) {
    if (!g_is_elevated) return false;
    g_is_elevated = false;
    users_sync_process_creds();
    serial_printf("[USERS] Privilegios de superusuario devolvidos.\n");
    return true;
}

bool users_add(const char *username, const char *password, bool is_admin) {
    if (!username || !username[0] || g_user_count >= MAX_SYSTEM_USERS) return false;
    if (users_get_by_name(username) != NULL) return false;

    /* Encontra proximo UID disponivel */
    uint32_t next_uid = 1000;
    for (int i = 0; i < g_user_count; i++) {
        if (g_users[i].uid >= next_uid) next_uid = g_users[i].uid + 1;
    }

    user_account_t *u = &g_users[g_user_count++];
    strncpy(u->username, username, USER_NAME_MAX - 1);
    u->username[USER_NAME_MAX - 1] = '\0';
    strncpy(u->password, password && password[0] ? password : "liwus", USER_PASS_MAX - 1);
    u->password[USER_PASS_MAX - 1] = '\0';
    u->uid = next_uid;
    u->gid = next_uid;
    lv_snprintf(u->home, sizeof(u->home), "/home/%s", username);
    strncpy(u->shell, "/bin/liwsh", sizeof(u->shell) - 1);
    u->is_admin = is_admin;

    sdfs_create_dir(u->home);
    users_save_to_disk();
    serial_printf("[USERS] Usuario criado: '%s' (UID %u, Admin %d)\n", username, u->uid, is_admin);
    return true;
}

bool users_del(const char *username) {
    if (!username || strcmp(username, "root") == 0) return false; /* Protege root */
    int idx = -1;
    for (int i = 0; i < g_user_count; i++) {
        if (strcmp(g_users[i].username, username) == 0) {
            idx = i;
            break;
        }
    }
    if (idx < 0) return false;

    for (int i = idx; i < g_user_count - 1; i++) {
        g_users[i] = g_users[i + 1];
    }
    g_user_count--;

    if (g_active_user_idx >= g_user_count) g_active_user_idx = 0;
    users_sync_process_creds();
    users_save_to_disk();
    serial_printf("[USERS] Usuario removido: '%s'\n", username);
    return true;
}

bool users_set_password(const char *username, const char *new_password) {
    if (!username || !new_password) return false;
    for (int i = 0; i < g_user_count; i++) {
        if (strcmp(g_users[i].username, username) == 0) {
            strncpy(g_users[i].password, new_password, USER_PASS_MAX - 1);
            g_users[i].password[USER_PASS_MAX - 1] = '\0';
            users_save_to_disk();
            return true;
        }
    }
    return false;
}

bool users_authenticate(const char *username, const char *password) {
    if (!username || !password) return false;
    const user_account_t *u = users_get_by_name(username);
    if (!u) return false;
    return strcmp(u->password, password) == 0;
}
