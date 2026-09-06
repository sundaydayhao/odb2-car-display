#include "elm327_protocol.h"

#include <ctype.h>
#include <string.h>

void elm_response_reset(elm_response_t *r) { memset(r, 0, sizeof(*r)); }

void elm_response_feed(elm_response_t *r, const uint8_t *bytes, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = bytes[i];
        if (r->complete) {
            if (c && !isspace(c)) r->invalid = true;
            continue;
        }
        if (c == '>') { r->complete = true; continue; }
        if (!c) continue;
        if ((c < 32 && c != '\r' && c != '\n' && c != '\t') || c > 126 || r->length + 1 >= sizeof(r->text)) {
            r->invalid = true;
            continue;
        }
        r->text[r->length++] = (char)toupper(c);
        r->text[r->length] = 0;
    }
}

static bool token(const elm_response_t *r, const char *word)
{
    if (!r->complete || r->invalid) return false;
    const char *p = r->text;
    while ((p = strstr(p, word))) {
        size_t length = strlen(word);
        if ((p == r->text || isspace((unsigned char)p[-1])) && (!p[length] || isspace((unsigned char)p[length]))) return true;
        ++p;
    }
    return false;
}

bool elm_response_is_ok(const elm_response_t *r) { return token(r, "OK"); }

bool elm_response_is_adapter(const elm_response_t *r)
{
    return r->complete && !r->invalid && (strstr(r->text, "ELM327") || strstr(r->text, "STN"));
}

bool elm_response_is_can_protocol(const elm_response_t *r)
{
    /* ATDPN returns 6..9 (ISO 15765-4), optionally prefixed by A for auto. */
    if (!r->complete || r->invalid) return false;
    const char *p = r->text;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) ++p;
        const char *end = p;
        while (*end && !isspace((unsigned char)*end)) ++end;
        size_t n = end - p;
        if ((n == 1 || (n == 2 && p[0] == 'A')) && end[-1] >= '6' && end[-1] <= '9') return true;
        p = end;
    }
    return false;
}

static int hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool frame(const char *line, size_t length, uint32_t *ecu, uint8_t *bytes, size_t *count)
{
    char compact[280];
    size_t n = 0;
    while (length && isspace((unsigned char)*line)) { ++line; --length; }
    if (length >= 12 && !strncmp(line, "SEARCHING...", 12)) { line += 12; length -= 12; }
    for (size_t i = 0; i < length; ++i) {
        if (isspace((unsigned char)line[i])) continue;
        if (hex(line[i]) < 0 || n + 1 >= sizeof(compact)) return false;
        compact[n++] = line[i];
    }
    compact[n] = 0;
    size_t offset = 0;
    *ecu = 0;
    if (n >= 7 && compact[0] == '7' && compact[1] == 'E' && hex(compact[2]) >= 8) offset = 3;
    else if (n >= 12 && !strncmp(compact, "18DAF1", 6)) offset = 8;
    for (size_t i = 0; i < offset; ++i) *ecu = (*ecu << 4) | hex(compact[i]);
    if (offset && (n - offset) % 2) {
        /* Some adapters add the optional one-nibble DLC after the header. */
        if (compact[offset] < '1' || compact[offset] > '8') return false;
        ++offset;
    }
    if (n <= offset || (n - offset) % 2 || (n - offset) / 2 > ELM_PAYLOAD_CAPACITY) return false;
    *count = (n - offset) / 2;
    for (size_t i = 0; i < *count; ++i) bytes[i] = (hex(compact[offset + i*2]) << 4) | hex(compact[offset + i*2 + 1]);
    return true;
}

typedef struct {
    uint32_t ecu;
    uint8_t bytes[ELM_PAYLOAD_CAPACITY];
    size_t total, used;
    uint8_t next;
    bool occupied;
} assembly_t;

static void consider(elm_pid_reply_t *reply, bool *found, uint8_t pid, uint32_t wanted, uint32_t ecu, const uint8_t *data, size_t length)
{
    if (length < 3 || data[0] != 0x41 || data[1] != pid || (wanted != ELM_ECU_ANY && wanted != ecu)) return;
    if (*found && (ecu == 0 || (reply->ecu != 0 && ecu >= reply->ecu))) return;
    reply->ecu = ecu;
    reply->length = length - 2;
    memcpy(reply->data, data + 2, reply->length);
    *found = true;
}

bool elm_decode_pid(const elm_response_t *r, uint8_t pid, uint32_t wanted, elm_pid_reply_t *reply)
{
    if (!r || !reply || !r->complete || r->invalid) return false;
    assembly_t assemblies[8] = {0};
    bool found = false;
    memset(reply, 0, sizeof(*reply));
    const char *p = r->text;
    while (*p) {
        const char *end = p;
        while (*end && *end != '\r' && *end != '\n') ++end;
        uint8_t bytes[ELM_PAYLOAD_CAPACITY];
        size_t count;
        uint32_t ecu;
        if (frame(p, end-p, &ecu, bytes, &count)) {
            if (!ecu && bytes[0] == 0x41) consider(reply,&found,pid,wanted,ecu,bytes,count);
            else if ((!ecu || count<=8) && bytes[0] > 0 && bytes[0] <= 7 && count >= (size_t)bytes[0]+1) {
                consider(reply,&found,pid,wanted,ecu,bytes+1,bytes[0]);
            } else if (ecu && count <= 8 && (bytes[0] >> 4) == 1 && count >= 3) {
                assembly_t *a = NULL;
                for (size_t i = 0; i < 8; ++i) if (assemblies[i].occupied && assemblies[i].ecu == ecu) a = &assemblies[i];
                if (!a) for (size_t i = 0; i < 8; ++i) if (!assemblies[i].occupied) { a = &assemblies[i]; break; }
                if (a) {
                    *a = (assembly_t){.ecu=ecu,.total=((bytes[0]&15)<<8)|bytes[1],.next=1,.occupied=true};
                    if (a->total > ELM_PAYLOAD_CAPACITY || a->total <= 7) a->total = 0;
                    else { a->used = count-2; memcpy(a->bytes,bytes+2,a->used); }
                }
            } else if (ecu && count <= 8 && count >= 2 && (bytes[0] >> 4) == 2) {
                for (size_t i = 0; i < 8; ++i) {
                    assembly_t *a = &assemblies[i];
                    if (!a->occupied || !a->total || a->ecu != ecu) continue;
                    if ((bytes[0]&15) != a->next) { a->total = 0; continue; }
                    size_t append = a->total-a->used;
                    if (append > count-1) append = count-1;
                    memcpy(a->bytes+a->used,bytes+1,append);
                    a->used += append;
                    a->next = (a->next+1)&15;
                    if (a->used == a->total) { consider(reply,&found,pid,wanted,ecu,a->bytes,a->total); a->total=0; }
                }
            }
        }
        p = *end ? end+1 : end;
    }
    return found;
}

bool elm_supported_pid(const elm_pid_reply_t *reply, uint8_t base, uint8_t pid)
{
    if (reply->length < 4 || pid <= base || (unsigned)pid > (unsigned)base+32) return false;
    unsigned index = pid-base-1;
    return (reply->data[index/8] & (0x80 >> (index%8))) != 0;
}
