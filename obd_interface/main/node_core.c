#include "node_core.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool output(node_core_t *c, const char *s) {
    if (!c->io.emit(c->io.ctx, s, strlen(s))) { c->output_failed = true; return false; }
    return true;
}
static void end(node_core_t *c, const char *s) { output(c, s); output(c, "\r>"); }
unsigned node_core_bitrate(const node_core_t *c) { return c->protocol >= 8 ? 250000 : 500000; }
static bool bus(node_core_t *c, node_bus_mode_t mode) {
    return c->io.bus(c->io.ctx, mode, node_core_bitrate(c));
}
static bool extended(const node_core_t *c) { return c->protocol == 7 || c->protocol == 9; }
static void defaults(node_core_t *c) {
    c->echo = true; c->headers = true; c->spaces = true;
    c->show_dlc = false; c->auto_format = true; c->auto_protocol = true;
    c->auto_locked = false;
    c->protocol = 6; c->timeout_ms = 400;
    c->header_set = c->filter_set = c->pending = c->monitor = c->received = false;
    c->length = 0; c->overflow = c->output_failed = false;
}
void node_core_init(node_core_t *c, const node_io_t *io) {
    memset(c, 0, sizeof(*c)); c->io = *io; defaults(c);
}
void node_core_disconnect(node_core_t *c) {
    defaults(c); c->connected = false;
    bus(c, NODE_BUS_LISTEN);
}
static bool hex(const char *s, size_t n, uint32_t *v) {
    if (strlen(s) != n) return false;
    uint32_t x = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned d = s[i] >= '0' && s[i] <= '9' ? (unsigned)(s[i] - '0') :
                     s[i] >= 'A' && s[i] <= 'F' ? (unsigned)(s[i] - 'A' + 10) : 16;
        if (d > 15) return false;
        x = (x << 4) | d;
    }
    *v = x; return true;
}
static void reject(node_core_t *c) { c->rejected++; end(c, "?"); }
static bool toggle(const char *s, const char *prefix, bool *value) {
    size_t n = strlen(prefix);
    if (strncmp(s, prefix, n) || strlen(s) != n + 1 || (s[n] != '0' && s[n] != '1')) return false;
    *value = s[n] == '1'; return true;
}
static bool request_pid(node_core_t *c, uint64_t now) {
    if (!bus(c, NODE_BUS_ACTIVE)) return false;
    node_frame_t f = {.id = c->header_set ? c->header : extended(c) ? 0x18DB33F1u : 0x7DF,
        .dlc = 8, .extended = extended(c), .data = {2, 1, c->pid, 0, 0, 0, 0, 0}};
    if (!c->io.transmit(c->io.ctx, &f)) { bus(c, NODE_BUS_LISTEN); return false; }
    c->pending = true; c->received = false; c->deadline = now + c->timeout_ms; c->requests++;
    return true;
}
static void execute(node_core_t *c, uint64_t now) {
    const char *s = c->command;
    uint32_t n;
    if (c->echo) { output(c, s); output(c, "\r"); }
    if (!strcmp(s, "ATZ") || !strcmp(s, "ATWS")) {
        defaults(c); bus(c, NODE_BUS_LISTEN); end(c, NODE_IDENT); return;
    }
    if (!strcmp(s, "ATI")) { end(c, NODE_IDENT); return; }
    if (!strcmp(s, "AT@1")) { end(c, "READ-ONLY OBD CAN/BLE NODE"); return; }
    if (!strcmp(s, "ATD")) { defaults(c); bus(c, NODE_BUS_LISTEN); end(c, "OK"); return; }
    if (toggle(s, "ATE", &c->echo) || toggle(s, "ATH", &c->headers) ||
        toggle(s, "ATS", &c->spaces) || toggle(s, "ATD", &c->show_dlc) ||
        toggle(s, "ATCAF", &c->auto_format)) { end(c, "OK"); return; }
    /* LF output is deliberately CR-only; ATL0 is the supported setting. */
    if (!strcmp(s, "ATL0") || !strcmp(s, "ATCFC1") || !strcmp(s, "ATAT0") ||
        !strcmp(s, "ATAT1") || !strcmp(s, "ATAT2")) { end(c, "OK"); return; }
    if (!strncmp(s, "ATST", 4) && hex(s + 4, 2, &n)) {
        c->timeout_ms = n ? n * 4 : 200; end(c, "OK"); return;
    }
    if (!strncmp(s, "ATSP", 4) && strlen(s) == 5 && (s[4] == '0' || (s[4] >= '6' && s[4] <= '9'))) {
        c->auto_protocol = s[4] == '0';
        c->protocol = c->auto_protocol ? (c->protocol >= 8 ? 8 : 6) : (unsigned)(s[4] - '0');
        c->auto_locked = false;
        c->header_set = c->filter_set = false;
        if (bus(c, NODE_BUS_LISTEN)) end(c, "OK"); else end(c, "CAN ERROR");
        return;
    }
    if (!strcmp(s, "ATDPN")) { char tmp[8]; snprintf(tmp, sizeof(tmp), "%s%u", c->auto_protocol ? "A" : "", c->protocol); end(c, tmp); return; }
    if (!strcmp(s, "ATDP")) { end(c, extended(c) ? "ISO 15765-4 CAN (29 BIT)" : "ISO 15765-4 CAN (11 BIT)"); return; }
    if (!strncmp(s, "ATSH", 4) && (hex(s + 4, 3, &n) || hex(s + 4, 8, &n))) {
        /* Whitelist emissions diagnostic request addresses. No arbitrary ECU writes. */
        bool allowed = extended(c) ? (n == 0x18DB33F1u || (n & 0x1FFF00FFu) == 0x18DA00F1u) : (n >= 0x7E0 && n <= 0x7E7) || n == 0x7DF;
        if (!allowed) { reject(c); return; }
        c->header = n; c->header_set = true; c->auto_locked = true; end(c, "OK"); return;
    }
    if (!strcmp(s, "ATCRA")) { c->filter_set = false; end(c, "OK"); return; }
    if (!strncmp(s, "ATCRA", 5) && (hex(s + 5, 3, &n) || hex(s + 5, 8, &n)) && n <= 0x1FFFFFFFu) {
        c->filter = n; c->filter_set = true; end(c, "OK"); return;
    }
    if (!strcmp(s, "ATRV")) {
        int mv = c->io.voltage_mv(c->io.ctx);
        if (mv < 0) { end(c, "NO DATA"); return; }
        char tmp[24]; snprintf(tmp, sizeof(tmp), "%d.%dV", mv / 1000, (mv % 1000) / 100); end(c, tmp); return;
    }
    if (!strcmp(s, "ATPC")) { bus(c, NODE_BUS_OFF); end(c, "OK"); return; }
    if (!strcmp(s, "ATMA")) {
        if (!bus(c, NODE_BUS_LISTEN)) { end(c, "CAN ERROR"); return; }
        c->monitor = true; c->output_failed = false; return; /* Prompt only on stop. */
    }
    if (!strcmp(s, "ATLOG1") || !strcmp(s, "ATLOG0")) {
        if (c->io.record(c->io.ctx, s[5] == '1')) end(c, "OK"); else end(c, "LOG ERROR");
        return;
    }
    if (!strcmp(s, "ATLOGFMT") || !strcmp(s, "ATLOGDUMP")) {
        bus(c, NODE_BUS_OFF);
        bool ok = c->io.log_command && c->io.log_command(c->io.ctx, !strcmp(s, "ATLOGFMT"));
        end(c, ok ? "OK" : "LOG ERROR"); return;
    }
    if (!strcmp(s, "ATSTAT")) {
        char tmp[400]; c->io.stats(c->io.ctx, tmp, sizeof(tmp)); output(c, tmp);
        snprintf(tmp, sizeof(tmp), "\rREQ=%lu REPLY=%lu NO_DATA=%lu REJECT=%lu",
                 (unsigned long)c->requests, (unsigned long)c->replies,
                 (unsigned long)c->no_data, (unsigned long)c->rejected); end(c, tmp); return;
    }
    if (!strncmp(s, "ATPOWER", 7) || !strncmp(s, "ATIDLE", 6) ||
        !strncmp(s, "ATWAKE", 6) || !strncmp(s, "ATVLOW", 6) || !strcmp(s, "ATSLEEP")) {
        char response[160];
        if (c->io.power_command && c->io.power_command(c->io.ctx, s, response, sizeof(response))) end(c, response);
        else reject(c);
        return;
    }
    if (strlen(s) == 4 && s[0] == '0' && s[1] == '1' && c->auto_format && hex(s + 2, 2, &n)) {
        if (c->auto_protocol && !c->auto_locked) c->protocol = c->protocol >= 8 ? 8 : 6;
        c->pid = n;
        if (!request_pid(c, now)) end(c, "CAN ERROR");
        return;
    }
    reject(c);
}
void node_core_feed(node_core_t *c, const uint8_t *p, size_t len, uint64_t now) {
    c->connected = true;
    if (c->monitor) { c->monitor = false; c->output_failed = false; end(c, "STOPPED"); return; }
    if (c->pending) return; /* Hosts must wait for >. Never interleave OBD transactions. */
    for (size_t i = 0; i < len; i++) {
        unsigned ch = p[i];
        if (ch == '\r' || ch == '\n') {
            if (c->overflow) { c->overflow = false; c->length = 0; reject(c); continue; }
            if (!c->length) continue;
            c->command[c->length] = 0; execute(c, now); c->length = 0;
            if (c->pending || c->monitor) break;
        } else if (ch == ' ' || ch == '\t') { continue; }
        else if (ch < 32 || ch > 126) { c->overflow = true; }
        else if (c->length < NODE_COMMAND_MAX) { c->command[c->length++] = (char)toupper(ch); }
        else c->overflow = true;
    }
}
static void frame_text(node_core_t *c, const node_frame_t *f, bool raw) {
    char line[80]; size_t used = 0;
    const char *sep = c->spaces ? " " : "";
    if (c->headers) used += snprintf(line + used, sizeof(line) - used, f->extended ? "%08lX%s" : "%03lX%s", (unsigned long)f->id, sep);
    if (c->show_dlc) used += snprintf(line + used, sizeof(line) - used, "%X%s", f->dlc, sep);
    if (f->rtr) used += snprintf(line + used, sizeof(line) - used, "RTR");
    else {
        unsigned first = raw || c->headers ? 0 : 1;
        unsigned count = raw ? f->dlc : (unsigned)f->data[0] + 1;
        if (count > f->dlc) count = f->dlc;
        for (unsigned i = first; i < count; i++) used += snprintf(line + used, sizeof(line) - used, "%02X%s", f->data[i], i + 1 < count ? sep : "");
    }
    snprintf(line + used, sizeof(line) - used, "\r"); output(c, line);
}
void node_core_receive(node_core_t *c, const node_frame_t *f, uint64_t now) {
    if (f->dlc > 8 || f->id > (f->extended ? 0x1FFFFFFFu : 0x7FFu)) return;
    if (c->monitor) { if (!c->filter_set || f->id == c->filter) frame_text(c, f, true); return; }
    if (!c->pending || now >= c->deadline || f->rtr || f->extended != extended(c)) return;
    bool ecu = f->extended ? (f->id & 0x1FFFFF00u) == 0x18DAF100u : f->id >= 0x7E8 && f->id <= 0x7EF;
    if (!ecu || (c->filter_set && f->id != c->filter)) return;
    /* Mode 01 single-PID replies fit ISO-TP single frames. Reject incomplete/FF payloads. */
    if (f->dlc < 4 || f->data[0] < 3 || f->data[0] > 7 || f->data[0] + 1 > f->dlc || f->data[1] != 0x41 || f->data[2] != c->pid) return;
    frame_text(c, f, false); c->received = true; c->auto_locked = true; c->replies++;
}
void node_core_tick(node_core_t *c, uint64_t now) {
    if (c->output_failed && c->monitor) {
        c->monitor = false; c->output_failed = false;
        end(c, "BUFFER FULL");
    }
    if (c->pending && now >= c->deadline) {
        /* Search addressing formats only. Never probe another baud rate on a live bus. */
        if (!c->received && c->auto_protocol && !c->auto_locked && !(c->protocol & 1)) {
            c->protocol++;
            if (request_pid(c, now)) return;
            c->pending = false; end(c, "CAN ERROR"); return;
        }
        c->pending = false; bus(c, NODE_BUS_LISTEN);
        if (!c->received) { c->no_data++; output(c, "NO DATA\r"); }
        output(c, ">");
    }
}
void node_core_bus_error(node_core_t *c) {
    bool notify = c->pending || c->monitor;
    c->pending = c->monitor = false; bus(c, NODE_BUS_OFF);
    if (notify) end(c, "CAN ERROR");
}
