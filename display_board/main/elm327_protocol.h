#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ELM_RESPONSE_CAPACITY 2048
#define ELM_PAYLOAD_CAPACITY 128
#define ELM_ECU_ANY UINT32_MAX

typedef struct {
    char text[ELM_RESPONSE_CAPACITY];
    size_t length;
    bool complete;
    bool invalid;
} elm_response_t;

typedef struct {
    uint32_t ecu;
    uint8_t data[ELM_PAYLOAD_CAPACITY];
    size_t length;
} elm_pid_reply_t;

void elm_response_reset(elm_response_t *response);
void elm_response_feed(elm_response_t *response, const uint8_t *bytes, size_t length);
bool elm_response_is_ok(const elm_response_t *response);
bool elm_response_is_adapter(const elm_response_t *response);
bool elm_response_is_can_protocol(const elm_response_t *response);
/* 0: ordinary adapter; 1: identified B02-Z challenge; -1: unsafe/invalid challenge.
 * Output capacity includes the NUL: 19 command bytes + NUL. */
int elm_adapter_handshake(const elm_response_t *response, char command[20]);
bool elm_decode_pid(const elm_response_t *response, uint8_t pid, uint32_t ecu, elm_pid_reply_t *reply);
bool elm_supported_pid(const elm_pid_reply_t *reply, uint8_t base, uint8_t pid);
