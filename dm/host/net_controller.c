// Net Controller: embedded HTTP + WebSocket server for Wi-Fi mobile gamepad input.
#include "net_controller.h"
#include "net_controller_html.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <CommonCrypto/CommonDigest.h>

#define MAX_CLIENTS 16
#define RX_BUF_SIZE 4096

typedef struct {
    int fd;
    bool is_ws;
    int assigned_player; // 0..7 or -1
    uint8_t buf[RX_BUF_SIZE];
    size_t len;
} ClientConn;

static pthread_t g_thread;
static volatile bool g_running = false;
static int g_listen_fd = -1;
static int g_pipe_ctl[2] = {-1, -1};
static int g_port = 8080;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static NetPadPlayerState g_states[NETPAD_MAX_PLAYERS];
static bool g_force_connected[NETPAD_MAX_PLAYERS];
static netpad_change_cb g_change_callback = NULL;

static const char B64_TABLE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64_encode(const unsigned char *src, size_t len, char *out) {
    size_t i = 0, j = 0;
    for (; i + 2 < len; i += 3) {
        out[j++] = B64_TABLE[(src[i] >> 2) & 0x3F];
        out[j++] = B64_TABLE[((src[i] & 0x3) << 4) | ((src[i + 1] >> 4) & 0xF)];
        out[j++] = B64_TABLE[((src[i + 1] & 0xF) << 2) | ((src[i + 2] >> 6) & 0x3)];
        out[j++] = B64_TABLE[src[i + 2] & 0x3F];
    }
    if (i < len) {
        out[j++] = B64_TABLE[(src[i] >> 2) & 0x3F];
        if (i + 1 < len) {
            out[j++] = B64_TABLE[((src[i] & 0x3) << 4) | ((src[i + 1] >> 4) & 0xF)];
            out[j++] = B64_TABLE[((src[i + 1] & 0xF) << 2)];
        } else {
            out[j++] = B64_TABLE[(src[i] & 0x3) << 4];
            out[j++] = '=';
        }
        out[j++] = '=';
    }
    out[j] = '\0';
}

void net_controller_get_local_ip(char *out_ip, size_t max_len) {
    struct ifaddrs *ifaddr, *ifa;
    snprintf(out_ip, max_len, "127.0.0.1");
    if (getifaddrs(&ifaddr) == -1) return;

    // Prefer en0 (standard Wi-Fi on Mac)
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (strcmp(ifa->ifa_name, "en0") == 0) {
            struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &(sa->sin_addr), out_ip, (socklen_t)max_len);
            freeifaddrs(ifaddr);
            return;
        }
    }
    // Fallback: any other non-loopback AF_INET interface
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (strcmp(ifa->ifa_name, "lo0") != 0) {
            struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &(sa->sin_addr), out_ip, (socklen_t)max_len);
            break;
        }
    }
    freeifaddrs(ifaddr);
}

int net_controller_get_port(void) {
    return g_port;
}

void net_controller_set_change_callback(netpad_change_cb cb) {
    pthread_mutex_lock(&g_lock);
    g_change_callback = cb;
    pthread_mutex_unlock(&g_lock);
}

static void notify_change(int player) {
    netpad_change_cb cb = NULL;
    pthread_mutex_lock(&g_lock);
    cb = g_change_callback;
    pthread_mutex_unlock(&g_lock);
    if (cb && player >= 0 && player < NETPAD_MAX_PLAYERS) {
        cb(player);
    }
}

uint32_t net_controller_player_mask(void) {
    uint32_t m = 0;
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < NETPAD_MAX_PLAYERS; i++) {
        if (g_states[i].connected || g_force_connected[i]) {
            m |= (1u << i);
        }
    }
    pthread_mutex_unlock(&g_lock);
    return m;
}

bool net_controller_get_state(int player, NetPadPlayerState *out_state) {
    if (player < 0 || player >= NETPAD_MAX_PLAYERS) return false;
    pthread_mutex_lock(&g_lock);
    bool conn = g_states[player].connected || g_force_connected[player];
    if (out_state) {
        *out_state = g_states[player];
        out_state->connected = conn;
    }
    pthread_mutex_unlock(&g_lock);
    return conn;
}

void net_controller_set_force_connect(int player, bool connected) {
    if (player < 0 || player >= NETPAD_MAX_PLAYERS) return;
    pthread_mutex_lock(&g_lock);
    bool changed = (g_force_connected[player] != connected);
    g_force_connected[player] = connected;
    pthread_mutex_unlock(&g_lock);
    if (changed) notify_change(player);
}

bool net_controller_is_force_connect(int player) {
    if (player < 0 || player >= NETPAD_MAX_PLAYERS) return false;
    pthread_mutex_lock(&g_lock);
    bool fc = g_force_connected[player];
    pthread_mutex_unlock(&g_lock);
    return fc;
}

void net_controller_set_state(int player, bool connected, uint64_t buttons,
                              int32_t lx, int32_t ly, int32_t rx, int32_t ry) {
    if (player < 0 || player >= NETPAD_MAX_PLAYERS) return;
    bool was_conn = false;
    pthread_mutex_lock(&g_lock);
    was_conn = g_states[player].connected || g_force_connected[player];
    g_states[player].connected = connected;
    g_states[player].buttons = buttons;
    g_states[player].stick_l_x = lx;
    g_states[player].stick_l_y = ly;
    g_states[player].stick_r_x = rx;
    g_states[player].stick_r_y = ry;
    bool now_conn = connected || g_force_connected[player];
    pthread_mutex_unlock(&g_lock);
    if (was_conn != now_conn) notify_change(player);
}

static void compute_ws_accept(const char *key, char *out_accept) {
    char combined[128];
    snprintf(combined, sizeof(combined), "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key);
    unsigned char hash[CC_SHA1_DIGEST_LENGTH];
    CC_SHA1((const unsigned char *)combined, (CC_LONG)strlen(combined), hash);
    base64_encode(hash, CC_SHA1_DIGEST_LENGTH, out_accept);
}

static void send_ws_frame(int fd, uint8_t opcode, const char *payload, size_t len) {
    uint8_t header[10];
    size_t hlen = 0;
    header[0] = 0x80 | (opcode & 0x0F);
    if (len <= 125) {
        header[1] = (uint8_t)len;
        hlen = 2;
    } else if (len <= 65535) {
        header[1] = 126;
        header[2] = (uint8_t)((len >> 8) & 0xFF);
        header[3] = (uint8_t)(len & 0xFF);
        hlen = 4;
    } else {
        header[1] = 127;
        for (int i = 0; i < 8; i++) {
            header[2 + i] = (uint8_t)((len >> ((7 - i) * 8)) & 0xFF);
        }
        hlen = 10;
    }
    send(fd, header, hlen, 0);
    if (len > 0 && payload) {
        send(fd, payload, len, 0);
    }
}

static long get_json_int(const char *json, const char *key, long default_val) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(json, search);
    if (!p) {
        snprintf(search, sizeof(search), "\"%s\" :", key);
        p = strstr(json, search);
    }
    if (!p) return default_val;
    p = strchr(p, ':');
    if (!p) return default_val;
    p++;
    while (*p == ' ') p++;
    return strtol(p, NULL, 0);
}

static uint64_t get_json_u64(const char *json, const char *key, uint64_t default_val) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(json, search);
    if (!p) {
        snprintf(search, sizeof(search), "\"%s\" :", key);
        p = strstr(json, search);
    }
    if (!p) return default_val;
    p = strchr(p, ':');
    if (!p) return default_val;
    p++;
    while (*p == ' ') p++;
    return strtoull(p, NULL, 0);
}

static void handle_client_json(ClientConn *c, const char *json) {
    if (strstr(json, "\"join\"")) {
        int p = (int)get_json_int(json, "p", 0);
        if (p >= 0 && p < NETPAD_MAX_PLAYERS) {
            c->assigned_player = p;
            pthread_mutex_lock(&g_lock);
            g_states[p].connected = true;
            g_force_connected[p] = true;
            pthread_mutex_unlock(&g_lock);
            notify_change(p);
            char resp[64];
            snprintf(resp, sizeof(resp), "{\"type\":\"assigned\",\"p\":%d,\"force\":true}", p);
            send_ws_frame(c->fd, 0x1, resp, strlen(resp));
        }
    } else if (strstr(json, "\"force\"")) {
        int p = (int)get_json_int(json, "p", c->assigned_player >= 0 ? c->assigned_player : 0);
        bool st = (strstr(json, "true") != NULL) || (get_json_int(json, "state", 0) != 0);
        net_controller_set_force_connect(p, st);
    } else if (strstr(json, "\"ping\"")) {
        const char *pong = "{\"type\":\"pong\"}";
        send_ws_frame(c->fd, 0x1, pong, strlen(pong));
    } else if (strstr(json, "\"input\"")) {
        int p = (int)get_json_int(json, "p", c->assigned_player >= 0 ? c->assigned_player : 0);
        if (p < 0 || p >= NETPAD_MAX_PLAYERS) p = 0;
        uint64_t b = get_json_u64(json, "b", 0);
        int32_t lx = (int32_t)get_json_int(json, "lx", 0);
        int32_t ly = (int32_t)get_json_int(json, "ly", 0);
        int32_t rx = (int32_t)get_json_int(json, "rx", 0);
        int32_t ry = (int32_t)get_json_int(json, "ry", 0);

        bool was_conn = false;
        pthread_mutex_lock(&g_lock);
        was_conn = g_states[p].connected || g_force_connected[p];
        g_states[p].connected = true;
        g_force_connected[p] = true;
        g_states[p].buttons = b;
        g_states[p].stick_l_x = lx;
        g_states[p].stick_l_y = ly;
        g_states[p].stick_r_x = rx;
        g_states[p].stick_r_y = ry;
        pthread_mutex_unlock(&g_lock);
        if (!was_conn) notify_change(p);
    }
}

static void handle_client_binary(ClientConn *c, const uint8_t *data, size_t len) {
    if (len < 20 || data[0] != 'N' || data[1] != 'P') return;
    int p = data[3];
    if (p < 0 || p >= NETPAD_MAX_PLAYERS) return;

    uint64_t b = *(const uint64_t *)(data + 4);
    int16_t lx = *(const int16_t *)(data + 12);
    int16_t ly = *(const int16_t *)(data + 14);
    int16_t rx = *(const int16_t *)(data + 16);
    int16_t ry = *(const int16_t *)(data + 18);

    bool was_conn = false;
    pthread_mutex_lock(&g_lock);
    was_conn = g_states[p].connected || g_force_connected[p];
    g_states[p].connected = true;
    g_force_connected[p] = true;
    g_states[p].buttons = b;
    g_states[p].stick_l_x = lx;
    g_states[p].stick_l_y = ly;
    g_states[p].stick_r_x = rx;
    g_states[p].stick_r_y = ry;
    pthread_mutex_unlock(&g_lock);
    if (!was_conn) notify_change(p);
}

static void close_client(ClientConn *c, ClientConn *all_clients, int max_c) {
    if (c->fd < 0) return;
    close(c->fd);
    c->fd = -1;
    int p = c->assigned_player;
    c->assigned_player = -1;
    c->is_ws = false;
    c->len = 0;

    if (p >= 0 && p < NETPAD_MAX_PLAYERS) {
        // Check if any other client still holds slot p
        bool other_using = false;
        for (int i = 0; i < max_c; i++) {
            if (all_clients[i].fd >= 0 && all_clients[i].assigned_player == p) {
                other_using = true;
                break;
            }
        }
        if (!other_using) {
            pthread_mutex_lock(&g_lock);
            g_states[p].connected = false;
            g_states[p].buttons = 0;
            g_states[p].stick_l_x = 0;
            g_states[p].stick_l_y = 0;
            g_states[p].stick_r_x = 0;
            g_states[p].stick_r_y = 0;
            pthread_mutex_unlock(&g_lock);
            notify_change(p);
        }
    }
}

static void process_ws_data(ClientConn *c, ClientConn *all_clients, int max_c) {
    while (c->len >= 2) {
        uint8_t b0 = c->buf[0];
        uint8_t b1 = c->buf[1];
        uint8_t opcode = b0 & 0x0F;
        bool masked = (b1 & 0x80) != 0;
        uint64_t payload_len = b1 & 0x7F;
        size_t header_len = 2;

        if (payload_len == 126) {
            if (c->len < 4) return;
            payload_len = ((uint64_t)c->buf[2] << 8) | c->buf[3];
            header_len = 4;
        } else if (payload_len == 127) {
            if (c->len < 10) return;
            payload_len = 0;
            for (int i = 0; i < 8; i++) payload_len = (payload_len << 8) | c->buf[2 + i];
            header_len = 10;
        }

        size_t mask_offset = header_len;
        if (masked) header_len += 4;
        if (c->len < header_len + payload_len) return; // Wait for full frame

        uint8_t *payload = c->buf + header_len;
        if (masked) {
            const uint8_t *mask = c->buf + mask_offset;
            for (size_t i = 0; i < payload_len; i++) {
                payload[i] ^= mask[i % 4];
            }
        }

        if (opcode == 0x8) { // Close
            close_client(c, all_clients, max_c);
            return;
        } else if (opcode == 0x9) { // Ping
            send_ws_frame(c->fd, 0xA, (const char *)payload, payload_len);
        } else if (opcode == 0x1) { // Text
            char tmp[4096];
            size_t copy_n = payload_len < sizeof(tmp) - 1 ? payload_len : sizeof(tmp) - 1;
            memcpy(tmp, payload, copy_n);
            tmp[copy_n] = '\0';
            handle_client_json(c, tmp);
        } else if (opcode == 0x2) { // Binary
            handle_client_binary(c, payload, payload_len);
        }

        size_t total_frame_len = header_len + payload_len;
        memmove(c->buf, c->buf + total_frame_len, c->len - total_frame_len);
        c->len -= total_frame_len;
    }
}

static void process_http_data(ClientConn *c, ClientConn *all_clients, int max_c) {
    c->buf[c->len] = '\0';
    char *headers_end = strstr((char *)c->buf, "\r\n\r\n");
    if (!headers_end) return;

    if (strcasestr((char *)c->buf, "upgrade: websocket") ||
        (strcasestr((char *)c->buf, "upgrade:") && strcasestr((char *)c->buf, "websocket"))) {
        char *key_hdr = strcasestr((char *)c->buf, "sec-websocket-key:");
        if (key_hdr) {
            char key[128] = {0};
            key_hdr += 18;
            while (*key_hdr == ' ') key_hdr++;
            char *key_end = strstr(key_hdr, "\r\n");
            if (key_end && (size_t)(key_end - key_hdr) < sizeof(key)) {
                memcpy(key, key_hdr, key_end - key_hdr);
                key[key_end - key_hdr] = '\0';
            }

            char accept_str[64] = {0};
            compute_ws_accept(key, accept_str);

            char resp[256];
            int rlen = snprintf(resp, sizeof(resp),
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Accept: %s\r\n\r\n", accept_str);
            send(c->fd, resp, (size_t)rlen, 0);

            c->is_ws = true;
            // Assign next available player slot
            int slot = 0;
            for (int p = 0; p < NETPAD_MAX_PLAYERS; p++) {
                bool in_use = false;
                for (int i = 0; i < max_c; i++) {
                    if (all_clients[i].fd >= 0 && all_clients[i].assigned_player == p) {
                        in_use = true; break;
                    }
                }
                if (!in_use) { slot = p; break; }
            }
            c->assigned_player = slot;
            pthread_mutex_lock(&g_lock);
            g_states[slot].connected = true;
            g_force_connected[slot] = true;
            pthread_mutex_unlock(&g_lock);
            notify_change(slot);

            char assign_msg[64];
            snprintf(assign_msg, sizeof(assign_msg), "{\"type\":\"assigned\",\"p\":%d,\"force\":true}", slot);
            send_ws_frame(c->fd, 0x1, assign_msg, strlen(assign_msg));

            size_t consumed = (headers_end + 4) - (char *)c->buf;
            memmove(c->buf, c->buf + consumed, c->len - consumed);
            c->len -= consumed;
            if (c->len > 0) process_ws_data(c, all_clients, max_c);
            return;
        }
    }

    // Serve HTTP HEAD /
    if (strncmp((char *)c->buf, "HEAD /", 6) == 0) {
        size_t htmllen = strlen(NETPAD_INDEX_HTML);
        char header[256];
        int hlen = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n\r\n", htmllen);
        send(c->fd, header, (size_t)hlen, 0);
        shutdown(c->fd, SHUT_WR);
        close_client(c, all_clients, max_c);
        return;
    }

    // Serve HTML for GET /
    if (strncmp((char *)c->buf, "GET /", 5) == 0) {
        size_t htmllen = strlen(NETPAD_INDEX_HTML);
        char header[256];
        int hlen = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n\r\n", htmllen);

        // Clear O_NONBLOCK and set SO_LINGER so transmission flushes completely
        fcntl(c->fd, F_SETFL, 0);
        struct linger sl = { .l_onoff = 1, .l_linger = 5 };
        setsockopt(c->fd, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl));

        send(c->fd, header, (size_t)hlen, 0);
        size_t sent = 0;
        while (sent < htmllen) {
            ssize_t s = send(c->fd, NETPAD_INDEX_HTML + sent, htmllen - sent, 0);
            if (s <= 0) break;
            sent += (size_t)s;
        }
        shutdown(c->fd, SHUT_WR);
        close_client(c, all_clients, max_c);
        return;
    }
}

static void *net_controller_worker(void *arg) {
    (void)arg;
    ClientConn clients[MAX_CLIENTS];
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = -1;
        clients[i].assigned_player = -1;
        clients[i].len = 0;
    }

    struct pollfd pfds[MAX_CLIENTS + 2];

    while (g_running) {
        pfds[0].fd = g_listen_fd;
        pfds[0].events = POLLIN;
        pfds[0].revents = 0;

        pfds[1].fd = g_pipe_ctl[0];
        pfds[1].events = POLLIN;
        pfds[1].revents = 0;

        int pcount = 2;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd >= 0) {
                pfds[pcount].fd = clients[i].fd;
                pfds[pcount].events = POLLIN;
                pfds[pcount].revents = 0;
                pcount++;
            }
        }

        int ret = poll(pfds, (nfds_t)pcount, 100);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (!g_running) break;

        // Check shutdown pipe
        if (pfds[1].revents & POLLIN) break;

        // Accept new client
        if (pfds[0].revents & POLLIN) {
            struct sockaddr_in caddr;
            socklen_t clen = sizeof(caddr);
            int cfd = accept(g_listen_fd, (struct sockaddr *)&caddr, &clen);
            if (cfd >= 0) {
                int nodelay = 1;
                setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
                fcntl(cfd, F_SETFL, O_NONBLOCK);

                int slot = -1;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].fd < 0) { slot = i; break; }
                }
                if (slot >= 0) {
                    clients[slot].fd = cfd;
                    clients[slot].is_ws = false;
                    clients[slot].assigned_player = -1;
                    clients[slot].len = 0;
                } else {
                    close(cfd);
                }
            }
        }

        // Process client sockets
        int curr = 2;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd < 0) continue;
            if (pfds[curr].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                close_client(&clients[i], clients, MAX_CLIENTS);
            } else if (pfds[curr].revents & POLLIN) {
                ssize_t n = recv(clients[i].fd, clients[i].buf + clients[i].len,
                                 sizeof(clients[i].buf) - 1 - clients[i].len, 0);
                if (n <= 0) {
                    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        curr++; continue;
                    }
                    close_client(&clients[i], clients, MAX_CLIENTS);
                } else {
                    clients[i].len += (size_t)n;
                    if (clients[i].is_ws) {
                        process_ws_data(&clients[i], clients, MAX_CLIENTS);
                    } else {
                        process_http_data(&clients[i], clients, MAX_CLIENTS);
                    }
                }
            }
            curr++;
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd >= 0) close(clients[i].fd);
    }
    return NULL;
}

int net_controller_init(void) {
    if (g_running) return 0;

    const char *port_env = getenv("DM_NETPAD_PORT");
    if (port_env && atoi(port_env) > 0) {
        g_port = atoi(port_env);
    }

    g_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen_fd < 0) {
        fprintf(stderr, "[NetPad] Falha ao criar socket\n");
        return -1;
    }

    int opt = 1;
    setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif
    fcntl(g_listen_fd, F_SETFL, O_NONBLOCK);

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);
    saddr.sin_port = htons((uint16_t)g_port);

    if (bind(g_listen_fd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        fprintf(stderr, "[NetPad] Falha ao fazer bind na porta %d (errno %d)\n", g_port, errno);
        close(g_listen_fd);
        g_listen_fd = -1;
        return -1;
    }

    if (listen(g_listen_fd, 8) < 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
        return -1;
    }

    if (pipe(g_pipe_ctl) < 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
        return -1;
    }
    fcntl(g_pipe_ctl[0], F_SETFL, O_NONBLOCK);

    g_running = true;
    if (pthread_create(&g_thread, NULL, net_controller_worker, NULL) != 0) {
        g_running = false;
        close(g_listen_fd);
        g_listen_fd = -1;
        close(g_pipe_ctl[0]);
        close(g_pipe_ctl[1]);
        return -1;
    }

    char local_ip[64];
    net_controller_get_local_ip(local_ip, sizeof(local_ip));
    fprintf(stderr, "\n[NetPad] Controles Wi-Fi ativos! Abra no celular: http://%s:%d\n\n", local_ip, g_port);

    return 0;
}

void net_controller_stop(void) {
    if (!g_running) return;
    g_running = false;
    if (g_pipe_ctl[1] >= 0) {
        char c = 'q';
        (void)write(g_pipe_ctl[1], &c, 1);
    }
    pthread_join(g_thread, NULL);
    if (g_listen_fd >= 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
    }
    if (g_pipe_ctl[0] >= 0) { close(g_pipe_ctl[0]); g_pipe_ctl[0] = -1; }
    if (g_pipe_ctl[1] >= 0) { close(g_pipe_ctl[1]); g_pipe_ctl[1] = -1; }
}
