#ifndef NET_NTP_CLIENT_H
#define NET_NTP_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Sincroniza o relogio via protocolo NTP (porta UDP 123).
 * Se server_ip_str for NULL ou vazio, tenta servidores padrao (roteador, Google NTP).
 * Retorna 0 em caso de sucesso, ou codigo de erro negativo. */
int ntp_sync(const char *server_ip_str);

/* Retorna o Unix timestamp atual (segundos desde 01/01/1970 UTC).
 * Utiliza o tempo sincronizado via NTP somado ao progresso do timer PIT. */
uint64_t ntp_get_unix_time(void);

/* Retorna se o relogio ja foi sincronizado com sucesso ao menos uma vez. */
bool ntp_is_synced(void);

/* Retorna a data e hora formatada em string legivel (ex: "2026-11-09 18:30:15 BRT").
 * tz_offset e o fuso horario em horas (ex: -3 para Brasilia). */
void ntp_get_formatted_time(char *buf, size_t max_len, int tz_offset);

/* Decompoem a data e hora atual em campos individuais. */
void ntp_datetime_get(int tz_offset, int *year, int *month, int *day,
                      int *hour, int *min, int *sec);

#endif /* NET_NTP_CLIENT_H */
