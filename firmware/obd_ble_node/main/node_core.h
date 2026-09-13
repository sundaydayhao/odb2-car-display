#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NODE_COMMAND_MAX 48
#define NODE_IDENT "OBDNode ELM327-compatible 0.1.0"
/* This identifies our compatibility interface, not genuine Elm Electronics silicon. */
typedef enum { NODE_BUS_OFF, NODE_BUS_LISTEN, NODE_BUS_ACTIVE } node_bus_mode_t;
typedef struct { uint32_t id; uint8_t dlc; bool extended, rtr; uint8_t data[8]; } node_frame_t;
typedef struct {
    bool (*emit)(void *, const char *, size_t);
    bool (*bus)(void *, node_bus_mode_t, unsigned bitrate);
    bool (*transmit)(void *, const node_frame_t *);
    int (*voltage_mv)(void *);
    bool (*record)(void *, bool enabled);
    void (*stats)(void *, char *, size_t);
    bool (*log_command)(void *, bool format);
    bool (*power_command)(void *, const char *, char *, size_t);
    void *ctx;
} node_io_t;
typedef struct {
    node_io_t io;
    char command[NODE_COMMAND_MAX + 1];
    size_t length;
    bool overflow, echo, headers, spaces, show_dlc, auto_format;
    bool pending, monitor, received, auto_protocol, auto_locked, connected;
    bool filter_set, header_set, output_failed;
    unsigned protocol, timeout_ms;
    uint32_t header, filter;
    uint8_t pid;
    uint64_t deadline;
    uint32_t requests, replies, no_data, rejected;
} node_core_t;

void node_core_init(node_core_t *, const node_io_t *);
void node_core_disconnect(node_core_t *);
void node_core_feed(node_core_t *, const uint8_t *, size_t, uint64_t now_ms);
void node_core_receive(node_core_t *, const node_frame_t *, uint64_t now_ms);
void node_core_tick(node_core_t *, uint64_t now_ms);
void node_core_bus_error(node_core_t *);
unsigned node_core_bitrate(const node_core_t *);
