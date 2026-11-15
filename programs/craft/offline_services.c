#include <stddef.h>
#include "auth.h"
#include "client.h"
#include "db.h"

/* Online identity/networking and SQLite persistence are isolated from this
 * first offline port. World generation and block editing still use Craft's
 * original code. */
int get_access_token(char *result, int length, char *username, char *token) {
    (void)username; (void)token;
    if (result && length > 0) result[0] = '\0';
    return 0;
}
void client_enable(void) {}
void client_disable(void) {}
int get_client_enabled(void) { return 0; }
void client_connect(char *hostname, int port) { (void)hostname; (void)port; }
void client_start(void) {}
void client_stop(void) {}
void client_send(char *data) { (void)data; }
char *client_recv(void) { return NULL; }
void client_version(int version) { (void)version; }
void client_login(const char *username, const char *token) { (void)username; (void)token; }
void client_position(float x,float y,float z,float rx,float ry) {(void)x;(void)y;(void)z;(void)rx;(void)ry;}
void client_chunk(int p,int q,int key) {(void)p;(void)q;(void)key;}
void client_block(int x,int y,int z,int w) {(void)x;(void)y;(void)z;(void)w;}
void client_light(int x,int y,int z,int w) {(void)x;(void)y;(void)z;(void)w;}
void client_sign(int x,int y,int z,int face,const char *text) {(void)x;(void)y;(void)z;(void)face;(void)text;}
void client_talk(const char *text) {(void)text;}

void db_enable(void) {}
void db_disable(void) {}
int db_init(char *path) { (void)path; return 0; }
int get_db_enabled(void) { return 0; }
void db_close(void) {}
void db_commit(void) {}
void db_auth_set(char *username,char *token) {(void)username;(void)token;}
int db_auth_select(char *username) {(void)username;return 0;}
void db_auth_select_none(void) {}
int db_auth_get(char *username,char *token,int tokenlen) {
    (void)username;(void)token;(void)tokenlen;return 0;
}
int db_auth_get_selected(char *username,int userlen,char *token,int tokenlen) {
    (void)username;(void)userlen;(void)token;(void)tokenlen;return 0;
}
void db_save_state(float x,float y,float z,float rx,float ry) {(void)x;(void)y;(void)z;(void)rx;(void)ry;}
int db_load_state(float *x,float *y,float *z,float *rx,float *ry) {(void)x;(void)y;(void)z;(void)rx;(void)ry;return 0;}
void db_insert_block(int p,int q,int x,int y,int z,int w) {(void)p;(void)q;(void)x;(void)y;(void)z;(void)w;}
void db_insert_light(int p,int q,int x,int y,int z,int w) {(void)p;(void)q;(void)x;(void)y;(void)z;(void)w;}
void db_insert_sign(int p,int q,int x,int y,int z,int face,const char *text) {(void)p;(void)q;(void)x;(void)y;(void)z;(void)face;(void)text;}
void db_delete_sign(int x,int y,int z,int face) {(void)x;(void)y;(void)z;(void)face;}
void db_delete_signs(int x,int y,int z) {(void)x;(void)y;(void)z;}
void db_delete_all_signs(void) {}
void db_load_blocks(Map *map,int p,int q) {(void)map;(void)p;(void)q;}
void db_load_lights(Map *map,int p,int q) {(void)map;(void)p;(void)q;}
void db_load_signs(SignList *list,int p,int q) {(void)list;(void)p;(void)q;}
int db_get_key(int p,int q) {(void)p;(void)q;return 0;}
void db_set_key(int p,int q,int key) {(void)p;(void)q;(void)key;}
void db_worker_start(void) {}
void db_worker_stop(void) {}
int db_worker_run(void *arg) {(void)arg;return 0;}
