#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { RDA5807_ADDRESS=0x10, RDA5807_MIN=875, RDA5807_MAX=1080 };
typedef enum { RDA_OK, RDA_IO, RDA_TIMEOUT, RDA_CANCELLED, RDA_NO_STATION, RDA_RANGE } rda_result_t;
typedef struct {
    void *context;
    bool (*write)(void *,const uint8_t *,size_t);
    bool (*read)(void *,uint8_t *,size_t);
    void (*wait_ms)(void *,unsigned);
    bool (*cancelled)(void *);
} rda_io_t;
typedef struct {
    uint16_t frequency;
    uint8_t rssi;
    bool stereo,station,stc,seek_failed;
} rda_status_t;
typedef struct {
    rda_io_t io;
    uint16_t reg[4]; /* Sequential write: registers 02..05, high byte first. */
    uint16_t frequency;
    uint8_t volume;
    bool listening;
} rda5807_t;

rda_result_t rda5807_init(rda5807_t *radio,const rda_io_t *io,unsigned volume);
rda_result_t rda5807_tune(rda5807_t *radio,unsigned frequency);
rda_result_t rda5807_seek(rda5807_t *radio);
rda_result_t rda5807_read(rda5807_t *radio,rda_status_t *status);
rda_result_t rda5807_audio(rda5807_t *radio,bool listening,unsigned volume);
rda_result_t rda5807_stop(rda5807_t *radio);
