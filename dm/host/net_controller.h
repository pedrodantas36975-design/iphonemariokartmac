#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define NETPAD_MAX_PLAYERS 8

typedef struct {
    bool connected;
    uint64_t buttons;
    int32_t stick_l_x;
    int32_t stick_l_y;
    int32_t stick_r_x;
    int32_t stick_r_y;
    float gyro_x, gyro_y, gyro_z;
    uint64_t last_update_ns;
} NetPadPlayerState;

typedef void (*netpad_change_cb)(int player);

int net_controller_init(void);
void net_controller_stop(void);

void net_controller_set_change_callback(netpad_change_cb cb);

uint32_t net_controller_player_mask(void);
bool net_controller_get_state(int player, NetPadPlayerState *out_state);

void net_controller_set_force_connect(int player, bool connected);
bool net_controller_is_force_connect(int player);

void net_controller_set_state(int player, bool connected, uint64_t buttons,
                              int32_t lx, int32_t ly, int32_t rx, int32_t ry);

void net_controller_get_local_ip(char *out_ip, size_t max_len);
int net_controller_get_port(void);
