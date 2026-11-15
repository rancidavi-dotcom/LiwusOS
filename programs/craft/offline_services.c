#include <stddef.h>
#include "auth.h"
#include "client.h"

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

