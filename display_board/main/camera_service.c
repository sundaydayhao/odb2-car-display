#include "camera_service.h"
#include "feature_config.h"
#if DEVICE_CAMERA_AVAILABLE
#include <errno.h>
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
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cloud_service.h"
#include "device_services.h"
#include "ui.h"
#include "media_library.h"

static atomic_bool s_enabled, s_photo_pending, s_saving;
void camera_set_enabled(bool enabled) { atomic_store(&s_enabled, enabled); if (!enabled) atomic_store(&s_photo_pending,false); }
bool camera_is_enabled(void) { return atomic_load(&s_enabled); }
bool camera_take_photo(void)
{
    if (!camera_is_enabled() || atomic_load(&s_saving)) return false;
    return !atomic_exchange(&s_photo_pending,true);
}

static bool camera_ioctl(int fd, int command, void *argument, const char *stage)
{
    if (ioctl(fd, command, argument) == 0) return true;
    int error = errno;
    ESP_LOGE("camera", "%s failed: errno=%d (%s)", stage, error, strerror(error));
    return false;
}

static bool configure_format(int fd, struct v4l2_format *format)
{
    *format = (struct v4l2_format){.type = V4L2_BUF_TYPE_VIDEO_CAPTURE};
    if (!camera_ioctl(fd, VIDIOC_G_FMT, format, "get initial format")) return false;
    ESP_LOGI("camera", "Initial format: type=%u size=%ux%u fourcc=" V4L2_FMT_STR,
             (unsigned)format->type, (unsigned)format->fmt.pix.width,
             (unsigned)format->fmt.pix.height, V4L2_FMT_STR_ARG(format->fmt.pix.pixelformat));
    /* esp_video 2.0.1's initial CSI format has no type set. G_FMT copies
     * that entire structure, overwriting the caller's CAPTURE type with 0.
     * Build a fresh request, preserving the sensor's native dimensions. */
    struct v4l2_format request = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .fmt.pix = {.width = format->fmt.pix.width, .height = format->fmt.pix.height,
                    .pixelformat = V4L2_PIX_FMT_RGB565},
    };
    if (!camera_ioctl(fd, VIDIOC_S_FMT, &request, "set RGB565 format")) return false;
    *format = (struct v4l2_format){.type = V4L2_BUF_TYPE_VIDEO_CAPTURE};
    return camera_ioctl(fd, VIDIOC_G_FMT, format, "get negotiated format");
}

static bool preview_stream(uint16_t *preview)
{
    int fd = open(BSP_CAMERA_DEVICE, O_RDONLY);
    if (fd < 0) {
        int error = errno;
        ESP_LOGE("camera", "Open %s failed: errno=%d (%s)", BSP_CAMERA_DEVICE, error, strerror(error));
        return false;
    }
    bool ok = false, streaming = false;
    unsigned received = 0, shown = 0;
    void *buffers[2] = {0};
    size_t lengths[2] = {0};
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    struct v4l2_format format;
    if (!configure_format(fd, &format)) goto done;
    const unsigned width = format.fmt.pix.width, height = format.fmt.pix.height;
    const unsigned stride = format.fmt.pix.bytesperline ? format.fmt.pix.bytesperline : width * 2;
    if (format.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB565 || !width || !height ||
        width > 2048 || height > 2048 || stride < width * 2 || stride > 8192) {
        ESP_LOGE("camera", "Unsupported format: size=%ux%u stride=%u fourcc=" V4L2_FMT_STR,
                 width, height, stride, V4L2_FMT_STR_ARG(format.fmt.pix.pixelformat));
        goto done;
    }
    ESP_LOGI("camera", "Capture format: RGB565 %ux%u stride=%u", width, height, stride);
    struct timeval timeout = {.tv_sec = 1};
    if (!camera_ioctl(fd, VIDIOC_S_DQBUF_TIMEOUT, &timeout, "set dequeue timeout")) goto done;
    struct v4l2_requestbuffers req = {.count = 2, .type = type, .memory = V4L2_MEMORY_MMAP};
    if (!camera_ioctl(fd, VIDIOC_REQBUFS, &req, "allocate capture buffers")) goto done;
    if (req.count != 2) { ESP_LOGE("camera", "Unexpected buffer count: %u", (unsigned)req.count); goto done; }
    for (int i = 0; i < 2; ++i) {
        struct v4l2_buffer buf = {.type = type, .memory = V4L2_MEMORY_MMAP, .index = i};
        if (!camera_ioctl(fd, VIDIOC_QUERYBUF, &buf, "query capture buffer")) goto done;
        lengths[i] = buf.length;
        buffers[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (!buffers[i] || buffers[i] == MAP_FAILED || lengths[i] < stride * height) {
            ESP_LOGE("camera", "Invalid mapped buffer: index=%d length=%u required=%u errno=%d",
                     i, (unsigned)lengths[i], stride * height, errno);
            goto done;
        }
        if (!camera_ioctl(fd, VIDIOC_QBUF, &buf, "queue initial buffer")) goto done;
    }
    if (!camera_ioctl(fd, VIDIOC_STREAMON, &type, "start capture")) goto done;
    streaming = true;
    unsigned out_w = CAMERA_PREVIEW_WIDTH, out_h = height * out_w / width;
    if (out_h > CAMERA_PREVIEW_HEIGHT) { out_h = CAMERA_PREVIEW_HEIGHT; out_w = width * out_h / height; }
    unsigned left = (CAMERA_PREVIEW_WIDTH - out_w) / 2, top = (CAMERA_PREVIEW_HEIGHT - out_h) / 2;
    int64_t next = 0;
    int64_t report_at = esp_timer_get_time() + 5000000;
    ESP_LOGI("camera", "Preview started: canvas=%ux%u image=%ux%u", CAMERA_PREVIEW_WIDTH,
             CAMERA_PREVIEW_HEIGHT, out_w, out_h);
    while (atomic_load(&s_enabled) && !cloud_ota_busy()) {
        struct v4l2_buffer buf = {.type = type, .memory = V4L2_MEMORY_MMAP};
        if (!camera_ioctl(fd, VIDIOC_DQBUF, &buf, "dequeue frame")) goto done;
        if (buf.index >= 2 || buf.bytesused > lengths[buf.index]) {
            ESP_LOGE("camera", "Invalid captured frame: index=%u bytes=%u", (unsigned)buf.index, (unsigned)buf.bytesused);
            goto done;
        }
        ++received;
        if ((buf.flags & V4L2_BUF_FLAG_DONE) && buf.bytesused >= stride * height && esp_timer_get_time() >= next) {
            if (atomic_exchange(&s_photo_pending,false)) {
                atomic_store(&s_saving,true);
                device_status_set(SERVICE_CAMERA,SERVICE_BUSY,"正在保存照片");
                media_library_save_photo(buffers[buf.index],width,height,stride);
                atomic_store(&s_saving,false);
            }
            memset(preview, 0, CAMERA_PREVIEW_WIDTH * CAMERA_PREVIEW_HEIGHT * 2);
            for (unsigned y = 0; y < out_h; ++y) {
                const uint16_t *row = (const uint16_t *)((const uint8_t *)buffers[buf.index] + (y * height / out_h) * stride);
                for (unsigned x = 0; x < out_w; ++x)
                    preview[(y + top) * CAMERA_PREVIEW_WIDTH + x + left] = row[x * width / out_w];
            }
            /* UI copies into its own persistent buffer before this frame is requeued. */
            ui_camera_frame(preview);
            if (++shown == 1) ESP_LOGI("camera", "First preview frame: bytes=%u", (unsigned)buf.bytesused);
            device_status_set(SERVICE_CAMERA, SERVICE_READY, "本地预览中");
            next = esp_timer_get_time() + 1000000 / CONFIG_DEVICE_CAMERA_PREVIEW_FPS;
        }
        if (!camera_ioctl(fd, VIDIOC_QBUF, &buf, "requeue frame")) goto done;
        if (esp_timer_get_time() >= report_at) {
            ESP_LOGI("camera", "Preview statistics: received=%u rendered=%u", received, shown);
            report_at = esp_timer_get_time() + 5000000;
        }
    }
    ok = true;
done:
    if (streaming && !camera_ioctl(fd, VIDIOC_STREAMOFF, &type, "stop capture")) ok = false;
    for (int i = 0; i < 2; ++i)
        if (buffers[i] && buffers[i] != MAP_FAILED) munmap(buffers[i], lengths[i]);
    if (close(fd)) { ESP_LOGE("camera", "Close failed: errno=%d", errno); ok = false; }
    ESP_LOGI("camera", "Preview stopped: ok=%d received=%u rendered=%u", ok, received, shown);
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
            if (!initialized) {
                esp_err_t result = bsp_camera_start(NULL);
                initialized = result == ESP_OK;
                ESP_LOGI("camera", "Camera initialization: %s", esp_err_to_name(result));
            }
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
#else
void camera_set_enabled(bool enabled) { (void)enabled; }
bool camera_is_enabled(void) { return false; }
bool camera_take_photo(void) { return false; }
esp_err_t camera_service_start(void) { return ESP_OK; }
#endif
