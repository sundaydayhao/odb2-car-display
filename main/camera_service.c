#include "camera_service.h"
#include <fcntl.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "bsp/esp-bsp.h"
#include "esp_video_ioctl.h"
#include "esp_video_device.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cloud_service.h"
#include "device_services.h"
#include "ui.h"

static atomic_bool s_enabled;
void camera_set_enabled(bool enabled) { atomic_store(&s_enabled, enabled); }
bool camera_is_enabled(void) { return atomic_load(&s_enabled); }

static bool preview_stream(uint16_t *preview)
{
    int fd = open(BSP_CAMERA_DEVICE, O_RDONLY);
    if (fd < 0) return false;
    bool ok = false, streaming = false;
    void *buffers[2] = {0};
    size_t lengths[2] = {0};
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    struct v4l2_format format = {.type = type};
    if (ioctl(fd, VIDIOC_G_FMT, &format)) goto done;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
    if (ioctl(fd, VIDIOC_S_FMT, &format) || ioctl(fd, VIDIOC_G_FMT, &format)) goto done;
    const unsigned width = format.fmt.pix.width, height = format.fmt.pix.height;
    const unsigned stride = format.fmt.pix.bytesperline ? format.fmt.pix.bytesperline : width * 2;
    if (format.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB565 || !width || !height ||
        width > 2048 || height > 2048 || stride < width * 2 || stride > 8192) goto done;
    struct timeval timeout = {.tv_sec = 1};
    if (ioctl(fd, VIDIOC_S_DQBUF_TIMEOUT, &timeout)) goto done;
    struct v4l2_requestbuffers req = {.count = 2, .type = type, .memory = V4L2_MEMORY_MMAP};
    if (ioctl(fd, VIDIOC_REQBUFS, &req) || req.count != 2) goto done;
    for (int i = 0; i < 2; ++i) {
        struct v4l2_buffer buf = {.type = type, .memory = V4L2_MEMORY_MMAP, .index = i};
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf)) goto done;
        lengths[i] = buf.length;
        buffers[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (!buffers[i] || buffers[i] == MAP_FAILED || lengths[i] < stride * height) goto done;
        if (ioctl(fd, VIDIOC_QBUF, &buf)) goto done;
    }
    if (ioctl(fd, VIDIOC_STREAMON, &type)) goto done;
    streaming = true;
    unsigned out_w = CAMERA_PREVIEW_WIDTH, out_h = height * out_w / width;
    if (out_h > CAMERA_PREVIEW_HEIGHT) { out_h = CAMERA_PREVIEW_HEIGHT; out_w = width * out_h / height; }
    unsigned left = (CAMERA_PREVIEW_WIDTH - out_w) / 2, top = (CAMERA_PREVIEW_HEIGHT - out_h) / 2;
    int64_t next = 0;
    while (atomic_load(&s_enabled) && !cloud_ota_busy()) {
        struct v4l2_buffer buf = {.type = type, .memory = V4L2_MEMORY_MMAP};
        if (ioctl(fd, VIDIOC_DQBUF, &buf)) goto done;
        if (buf.index >= 2 || buf.bytesused > lengths[buf.index]) goto done;
        if ((buf.flags & V4L2_BUF_FLAG_DONE) && buf.bytesused >= stride * height && esp_timer_get_time() >= next) {
            memset(preview, 0, CAMERA_PREVIEW_WIDTH * CAMERA_PREVIEW_HEIGHT * 2);
            for (unsigned y = 0; y < out_h; ++y) {
                const uint16_t *row = (const uint16_t *)((const uint8_t *)buffers[buf.index] + (y * height / out_h) * stride);
                for (unsigned x = 0; x < out_w; ++x)
                    preview[(y + top) * CAMERA_PREVIEW_WIDTH + x + left] = row[x * width / out_w];
            }
            /* UI copies into its own persistent buffer before this frame is requeued. */
            ui_camera_frame(preview);
            device_status_set(SERVICE_CAMERA, SERVICE_READY, "本地预览中");
            next = esp_timer_get_time() + 1000000 / CONFIG_DEVICE_CAMERA_PREVIEW_FPS;
        }
        if (ioctl(fd, VIDIOC_QBUF, &buf)) goto done;
    }
    ok = true;
done:
    if (streaming) ioctl(fd, VIDIOC_STREAMOFF, &type);
    for (int i = 0; i < 2; ++i)
        if (buffers[i] && buffers[i] != MAP_FAILED) munmap(buffers[i], lengths[i]);
    close(fd);
    return ok;
}

static void camera_task(void *arg)
{
    (void)arg;
    bool initialized = false;
    uint16_t *preview = heap_caps_malloc(CAMERA_PREVIEW_WIDTH * CAMERA_PREVIEW_HEIGHT * 2, MALLOC_CAP_SPIRAM);
    if (!preview) {
        device_status_set(SERVICE_CAMERA, SERVICE_ERROR, "预览内存不足");
        vTaskDelete(NULL);
        return;
    }
    while (1) {
        if (atomic_load(&s_enabled) && !cloud_ota_busy()) {
            device_status_set(SERVICE_CAMERA, SERVICE_BUSY, "正在启动摄像头");
            if (!initialized) initialized = bsp_camera_start(NULL) == ESP_OK;
            bool ok = initialized && preview_stream(preview);
            atomic_store(&s_enabled, false);
            ui_camera_clear();
            device_status_set(SERVICE_CAMERA, ok ? SERVICE_OFF : SERVICE_ERROR,
                              ok ? "摄像头已关闭" : "摄像头连接或格式异常");
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t camera_service_start(void)
{
    device_status_set(SERVICE_CAMERA, SERVICE_OFF, "摄像头已关闭");
    return xTaskCreate(camera_task, "camera", 6144, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
