#include "liwus/liwus.h"
#include "liwus/fcntl.h"
#include "liwus/stdlib.h"
#include "liwus/string.h"
#include "liwus/stdio.h"
#include "liwus/unistd.h"
#include "liwus/syscall.h"
#include "net/liwustube.h"
#include <stdbool.h>

#define TUBE_TMP_JSON "/tmp/tube_query.json"
#define TUBE_TMP_MP4  "/tmp/tube_stream.mp4"
#define TUBE_CONF     "/etc/tube.conf"

static void print_str(int fd, const char *str) {
    if (!str) return;
    size_t len = strlen(str);
    while (len > 0) {
        long n = liw_write(fd, str, len);
        if (n <= 0) break;
        str += (size_t)n;
        len -= (size_t)n;
    }
}

static char *read_file_all(const char *path, size_t *out_size) {
    int fd = liw_open(path, LIW_O_RDONLY);
    if (fd < 0) return NULL;

    struct liw_stat st;
    if (liw_fstat(fd, &st) < 0 || st.st_size <= 0) {
        liw_close(fd);
        return NULL;
    }

    size_t sz = (size_t)st.st_size;
    char *buf = (char *)malloc(sz + 1);
    if (!buf) {
        liw_close(fd);
        return NULL;
    }

    size_t done = 0;
    while (done < sz) {
        long n = liw_read(fd, buf + done, sz - done);
        if (n <= 0) break;
        done += (size_t)n;
    }
    liw_close(fd);
    buf[done] = '\0';
    if (out_size) *out_size = done;
    return buf;
}

static void load_config_if_present(void) {
    size_t sz = 0;
    char *cfg = read_file_all(TUBE_CONF, &sz);
    if (cfg) {
        char *line = cfg;
        while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n') line++;
        char *end = line;
        while (*end && *end != '\r' && *end != '\n') end++;
        *end = '\0';
        if (line[0]) {
            liwustube_set_api(line);
        }
        free(cfg);
    }
}

static int cmd_search(const char *query) {
    int cand_cnt = liwustube_get_candidate_count();
    bool found = false;
    liwustube_search_result_t res;

    print_str(1, "[LiwusTube] Pesquisando: \"");
    print_str(1, query);
    print_str(1, "\"...\n");

    for (int c = 0; c < cand_cnt; c++) {
        const char *api = liwustube_get_candidate_api(c);
        char url[LIWUSTUBE_MAX_URL];
        if (liwustube_build_search_url(api, query, url, sizeof(url)) != 0) continue;

        print_str(1, "[LiwusTube] Tentando API: ");
        print_str(1, api);
        print_str(1, "...\n");

        long dl = liw_net_http_get(url, TUBE_TMP_JSON);
        if (dl <= 0) continue;

        size_t json_len = 0;
        char *json = read_file_all(TUBE_TMP_JSON, &json_len);
        if (!json) continue;

        if (liwustube_parse_search_json(json, json_len, &res) == 0 && res.count > 0) {
            found = true;
            liwustube_set_api(api);
            free(json);
            break;
        }
        free(json);
    }

    if (!found) {
        print_str(2, "[LiwusTube] Erro: Falha ao consultar busca na API em todos os endpoints.\n"
                     "[LiwusTube] Dica: Certifique-se de que a rede esta ativa e execute o bridge local:\n"
                     "            python3 scripts/tube_bridge.py\n"
                     "            tube --bridge http://10.0.2.2:8085 search \"...\n");
        return 1;
    }

    print_str(1, "\n================================================================================\n");
    print_str(1, "ID           TEMPO    CANAL                TITULO\n");
    print_str(1, "================================================================================\n");

    for (int i = 0; i < res.count; i++) {
        liwustube_search_item_t *it = &res.items[i];

        /* ID (11 chars fixos) */
        char id_buf[14];
        snprintf(id_buf, sizeof(id_buf), "%-12s ", it->id);
        print_str(1, id_buf);

        /* Tempo */
        char dur_buf[12];
        char dur_str[16];
        liwustube_format_duration(it->duration_sec, dur_str, sizeof(dur_str));
        snprintf(dur_buf, sizeof(dur_buf), "%-8s ", dur_str);
        print_str(1, dur_buf);

        /* Canal (truncado em 20 chars) */
        char ch_buf[24];
        char ch_trunc[21];
        strncpy(ch_trunc, it->author[0] ? it->author : "Desconhecido", 20);
        ch_trunc[20] = '\0';
        snprintf(ch_buf, sizeof(ch_buf), "%-20s ", ch_trunc);
        print_str(1, ch_buf);

        /* Título */
        print_str(1, it->title);
        print_str(1, "\n");
    }

    print_str(1, "================================================================================\n");
    print_str(1, "Para assistir, execute: tube play <ID>\n\n");
    return 0;
}

static int cmd_resolve(const char *input, liwustube_stream_info_t *out_info) {
    char id[LIWUSTUBE_MAX_ID];
    if (liwustube_extract_video_id(input, id, sizeof(id)) != 0) {
        print_str(2, "[LiwusTube] Erro: ID ou URL de video invalida\n");
        return -1;
    }

    int cand_cnt = liwustube_get_candidate_count();
    bool found = false;

    print_str(1, "[LiwusTube] Resolvendo video ID: ");
    print_str(1, id);
    print_str(1, "...\n");

    for (int c = 0; c < cand_cnt; c++) {
        const char *api = liwustube_get_candidate_api(c);
        char url[LIWUSTUBE_MAX_URL];
        if (liwustube_build_stream_url(api, id, url, sizeof(url)) != 0) continue;

        print_str(1, "[LiwusTube] Tentando API: ");
        print_str(1, api);
        print_str(1, "...\n");

        long dl = liw_net_http_get(url, TUBE_TMP_JSON);
        if (dl <= 0) continue;

        size_t json_len = 0;
        char *json = read_file_all(TUBE_TMP_JSON, &json_len);
        if (!json) continue;

        if (liwustube_parse_stream_json(json, json_len, out_info) == 0 && out_info->stream_url[0]) {
            found = true;
            liwustube_set_api(api);
            free(json);
            break;
        }
        free(json);
    }

    if (!found) {
        print_str(2, "[LiwusTube] Nenhuma stream compativel (MP4 360p) encontrada para este video.\n"
                     "[LiwusTube] Dica: Inicie o bridge local no host para assistir qualquer video:\n"
                     "            python3 scripts/tube_bridge.py\n");
        return -1;
    }
    return 0;
}

static int cmd_get(const char *input) {
    liwustube_stream_info_t info;
    if (cmd_resolve(input, &info) != 0) return 1;

    print_str(1, "\n[LiwusTube] Metadados Encontrados:\n");
    print_str(1, "  Titulo: "); print_str(1, info.title); print_str(1, "\n");
    print_str(1, "  Canal:  "); print_str(1, info.author); print_str(1, "\n");

    char dur[16];
    liwustube_format_duration(info.duration_sec, dur, sizeof(dur));
    print_str(1, "  Duracao: "); print_str(1, dur); print_str(1, "\n");
    print_str(1, "  Qualidade: "); print_str(1, info.quality); print_str(1, "\n");
    print_str(1, "  Container: "); print_str(1, info.container); print_str(1, "\n");
    print_str(1, "  Stream URL: "); print_str(1, info.stream_url); print_str(1, "\n\n");
    return 0;
}

static int cmd_play(const char *input) {
    liwustube_stream_info_t info;
    if (cmd_resolve(input, &info) != 0) return 1;

    print_str(1, "[LiwusTube] Baixando stream progressiva: ");
    print_str(1, info.title);
    print_str(1, "\n[LiwusTube] Stream URL: ");
    print_str(1, info.stream_url);
    print_str(1, "\n[LiwusTube] Aguarde o download do video MP4...\n");

    long dl = liw_net_http_get(info.stream_url, TUBE_TMP_MP4);
    if (dl <= 0) {
        print_str(2, "[LiwusTube] Erro ao baixar stream de video\n");
        return 1;
    }

    print_str(1, "[LiwusTube] Concluido! Iniciando reproducao no Player...\n");
    char *const args[] = {"play", TUBE_TMP_MP4, NULL};
    (void)liw_execve("/bin/play.lxe", args, NULL);
    return 0;
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    load_config_if_present();

    int arg_idx = 1;
    while (arg_idx < argc) {
        if (liw_strcmp(argv[arg_idx], "--bridge") == 0 || liw_strcmp(argv[arg_idx], "--server") == 0) {
            if (arg_idx + 1 < argc) {
                liwustube_set_api(argv[arg_idx + 1]);
                arg_idx += 2;
                continue;
            }
        }
        break;
    }

    if (arg_idx >= argc) {
        print_str(2, "Uso: tube search \"termo de busca\"\n"
                     "     tube get <ID ou URL do YouTube>\n"
                     "     tube play <ID ou URL do YouTube>\n"
                     "     tube --bridge http://10.0.2.2:8085 <comando>\n");
        return 2;
    }

    if (liw_strcmp(argv[arg_idx], "search") == 0) {
        if (arg_idx + 1 >= argc) {
            print_str(2, "Uso: tube search \"termo de busca\"\n");
            return 2;
        }
        return cmd_search(argv[arg_idx + 1]);
    } else if (liw_strcmp(argv[arg_idx], "get") == 0) {
        if (arg_idx + 1 >= argc) {
            print_str(2, "Uso: tube get <ID ou URL>\n");
            return 2;
        }
        return cmd_get(argv[arg_idx + 1]);
    } else if (liw_strcmp(argv[arg_idx], "play") == 0) {
        if (arg_idx + 1 >= argc) {
            print_str(2, "Uso: tube play <ID ou URL>\n");
            return 2;
        }
        return cmd_play(argv[arg_idx + 1]);
    } else {
        /* Se passar direto o ID ou URL, assume play */
        return cmd_play(argv[arg_idx]);
    }
}
