#include "media_library.h"
#include "feature_config.h"
#include "recorder.h"
#include "device_services.h"
#include "photo_bmp.h"
#include "ui.h"
#include <dirent.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define PHOTO_DIR BSP_SD_MOUNT_POINT "/photos"
typedef struct { av_file_t file; char path[AV_FILE_PATH_SIZE]; } entry_t;
typedef struct { char path[AV_FILE_PATH_SIZE]; uint32_t generation, sequence; } photo_job_t;
static SemaphoreHandle_t s_lock;
static QueueHandle_t s_photo_jobs;
static entry_t *s_entries;
static av_library_status_t s_status;
static atomic_bool s_refresh;
static atomic_uint s_photo_sequence;
static unsigned s_photo_counter;
static uint32_t s_generation;

static void photo_detail(const char *detail)
{
    xSemaphoreTake(s_lock,portMAX_DELAY);
    snprintf(s_status.photo_detail,sizeof(s_status.photo_detail),"%s",detail);
    xSemaphoreGive(s_lock);
}
void media_library_refresh(void) { atomic_store(&s_refresh,true); }
void media_library_status(av_library_status_t *out)
{
    memset(out,0,sizeof(*out));
    if (!s_lock) return;
    xSemaphoreTake(s_lock,portMAX_DELAY); *out=s_status; xSemaphoreGive(s_lock);
}
size_t media_library_files(av_file_kind_t kind, unsigned first, av_file_t *files,
                           size_t capacity, uint32_t revision)
{
    if (!s_lock || !files || kind>AV_FILES_PHOTOS) return 0;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    unsigned count=kind==AV_FILES_MUSIC?s_status.music_count:s_status.photo_count;
    size_t n=0;
    if (s_entries && revision==s_status.revision && s_status.storage)
        while (n<capacity && first+n<count) { files[n]=s_entries[kind*AV_FILE_LIMIT+first+n].file; ++n; }
    xSemaphoreGive(s_lock); return n;
}
static bool get_file(av_file_kind_t kind, unsigned index, uint32_t revision, entry_t *entry, uint32_t *generation)
{
    if (!s_lock) return false;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    unsigned count=kind==AV_FILES_MUSIC?s_status.music_count:s_status.photo_count;
    bool ok=s_entries && s_status.storage && index<count && revision==s_status.revision;
    if (ok) { *entry=s_entries[kind*AV_FILE_LIMIT+index]; *generation=s_generation; }
    xSemaphoreGive(s_lock); return ok;
}
bool media_library_music(unsigned index, uint32_t revision, char *path, size_t path_size,
                          char *title, size_t title_size, uint32_t *generation)
{
    entry_t entry;
    if (!get_file(AV_FILES_MUSIC,index,revision,&entry,generation)) return false;
    if (strlen(entry.path)>=path_size || strlen(entry.file.name)>=title_size) return false;
    strcpy(path,entry.path); strcpy(title,entry.file.name); return true;
}
static bool extension(const char *name, const char *suffix)
{
    const char *dot=strrchr(name,'.'); return dot && !strcasecmp(dot,suffix);
}
static int compare_entries(const void *a, const void *b)
{ return strcasecmp(((const entry_t *)a)->file.name,((const entry_t *)b)->file.name); }
static void scan_dir(const char *path, const char *suffix, entry_t *entries, uint16_t *count, bool *truncated)
{
    DIR *dir=opendir(path);
    if (!dir) return;
    struct dirent *de;
    unsigned examined=0;
    while ((de=readdir(dir)) && recorder_storage_ready()) {
        if (++examined>4096) { *truncated=true; break; }
        if (!extension(de->d_name,suffix)) continue;
        if (strlen(de->d_name)>=AV_FILE_NAME_SIZE || strlen(path)+strlen(de->d_name)+2>AV_FILE_PATH_SIZE) {
            *truncated=true; continue;
        }
        if (*count>=AV_FILE_LIMIT) { *truncated=true; break; }
        entry_t *entry=&entries[*count];
        strcpy(entry->path,path); strcat(entry->path,"/"); strcat(entry->path,de->d_name);
        struct stat info;
        if (stat(entry->path,&info) || !S_ISREG(info.st_mode)) continue;
        strcpy(entry->file.name,de->d_name);
        ++*count;
    }
    closedir(dir);
}
static void scan_files(void)
{
    xSemaphoreTake(s_lock,portMAX_DELAY); s_status.scanning=true; xSemaphoreGive(s_lock);
    entry_t *entries=heap_caps_calloc(2*AV_FILE_LIMIT,sizeof(entry_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    uint16_t music=0,photos=0; bool truncated=false, ready=false; uint32_t generation=0;
    if (entries && recorder_storage_acquire()) {
        generation=recorder_storage_generation();
        scan_dir(BSP_SD_MOUNT_POINT,".mp3",entries,&music,&truncated);
        scan_dir(BSP_SD_MOUNT_POINT "/music",".mp3",entries,&music,&truncated);
        if (DEVICE_CAMERA_AVAILABLE) scan_dir(PHOTO_DIR,".bmp",entries+AV_FILE_LIMIT,&photos,&truncated);
        ready=recorder_storage_ready();
        recorder_storage_release();
        qsort(entries,music,sizeof(entry_t),compare_entries);
        qsort(entries+AV_FILE_LIMIT,photos,sizeof(entry_t),compare_entries);
    }
    xSemaphoreTake(s_lock,portMAX_DELAY);
    entry_t *old=s_entries; s_entries=entries; s_generation=generation;
    s_status.storage=ready; s_status.scanning=false; s_status.truncated=truncated;
    s_status.music_count=ready?music:0; s_status.photo_count=ready?photos:0; ++s_status.revision;
    snprintf(s_status.detail,sizeof(s_status.detail),"%s",!entries?"文件列表内存不足":!ready?"请插入 SD 卡":
             truncated?"列表最多显示各 128 个文件":music?"已找到 SD 卡音乐":"将 MP3 放入卡根目录或 music 文件夹");
    xSemaphoreGive(s_lock); free(old);
    ESP_LOGI("library","Scan: ready=%d generation=%lu music=%u photos=%u",ready,(unsigned long)generation,music,photos);
}
int media_library_read(const char *path, uint32_t generation, uint32_t offset, void *buffer, size_t capacity)
{
    if (!buffer || !capacity || offset>0x7fffffff || !recorder_storage_acquire()) return -1;
    int result=-1; bool io_error=false;
    if (generation==recorder_storage_generation()) {
        FILE *file=fopen(path,"rb");
        if (file) {
            if (!fseek(file,(long)offset,SEEK_SET)) {
                size_t n=fread(buffer,1,capacity,file);
                io_error=ferror(file); if (!io_error) result=(int)n;
            }
            if (fclose(file)) { io_error=true; result=-1; }
        }
    }
    recorder_storage_release();
    if (io_error) recorder_storage_report_error();
    return result;
}
bool media_library_view_photo(unsigned index, uint32_t revision)
{
    entry_t entry; photo_job_t job={0};
    if (!s_photo_jobs || !get_file(AV_FILES_PHOTOS,index,revision,&entry,&job.generation)) return false;
    strcpy(job.path,entry.path); job.sequence=atomic_fetch_add(&s_photo_sequence,1)+1;
    if (xQueueOverwrite(s_photo_jobs,&job)!=pdTRUE) return false;
    photo_detail("正在读取照片"); return true;
}
bool media_library_save_photo(const uint16_t *pixels, unsigned width, unsigned height, unsigned stride)
{
    if (!DEVICE_CAMERA_AVAILABLE) return false;
    if (!s_lock || !recorder_storage_acquire()) { if (s_lock) photo_detail("请先插入 SD 卡"); return false; }
    bool ok=false, io_error=false; char path[AV_FILE_PATH_SIZE], temp[AV_FILE_PATH_SIZE];
    uint8_t *row=heap_caps_malloc(6144,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    av_library_status_t status; media_library_status(&status);
    if (status.photo_count>=AV_FILE_LIMIT) { photo_detail("相册已达上限，请在电脑上整理"); goto done; }
    photo_detail("正在保存照片");
    if (!row || (mkdir(PHOTO_DIR,0770) && errno!=EEXIST)) { photo_detail("照片保存失败，请检查存储卡"); goto done; }
    snprintf(path,sizeof(path),PHOTO_DIR "/img-%s-%08x.bmp",device_boot_id(),s_photo_counter++);
    snprintf(temp,sizeof(temp),"%.*s.part",(int)strlen(path)-4,path);
    FILE *file=fopen(temp,"wx");
    if (!file) { photo_detail("照片文件无法创建"); goto done; }
    ok=photo_bmp_write(file,pixels,width,height,stride,row,6144);
    if (fflush(file) || fsync(fileno(file))) ok=false;
    io_error=ferror(file);
    if (fclose(file)) { ok=false; io_error=true; }
    if (ok) ok=rename(temp,path)==0;
    if (!ok) unlink(temp); /* Only this incomplete, newly created photo. */
    photo_detail(ok?"照片已保存到 photos 文件夹":"照片保存失败，请检查存储卡");
    ESP_LOGI("library","Photo save: ok=%d size=%ux%u",ok,width,height);
done:
    free(row); recorder_storage_release();
    if (io_error) recorder_storage_report_error();
    if (ok) media_library_refresh();
    return ok;
}
static void library_task(void *arg)
{
    (void)arg;
    uint16_t *pixels=DEVICE_CAMERA_AVAILABLE?heap_caps_malloc(AV_PREVIEW_WIDTH*AV_PREVIEW_HEIGHT*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT):NULL;
    uint8_t *row=DEVICE_CAMERA_AVAILABLE?heap_caps_malloc(6144,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT):NULL;
    bool ready=false; uint32_t generation=0;
    for (;;) {
        bool current=recorder_storage_ready(); uint32_t gen=recorder_storage_generation();
        if (atomic_exchange(&s_refresh,false) || current!=ready || (current && gen!=generation)) {
            scan_files(); ready=current; generation=gen;
        }
        photo_job_t job;
        if (!s_photo_jobs) { vTaskDelay(pdMS_TO_TICKS(250)); continue; }
        if (xQueueReceive(s_photo_jobs,&job,pdMS_TO_TICKS(250))!=pdTRUE) continue;
        bool ok=false, io_error=false;
        if (pixels && row && recorder_storage_acquire()) {
            if (job.generation==recorder_storage_generation()) {
                FILE *file=fopen(job.path,"rb");
                if (file) { ok=photo_bmp_preview(file,pixels,AV_PREVIEW_WIDTH,AV_PREVIEW_HEIGHT,row,6144); io_error=ferror(file); if (fclose(file)) io_error=true; }
            }
            recorder_storage_release();
        }
        if (io_error) recorder_storage_report_error();
        if (job.sequence!=atomic_load(&s_photo_sequence)) continue;
        if (ok && !io_error) { ui_media_photo_frame(pixels); photo_detail(strrchr(job.path,'/')+1); }
        else photo_detail("照片无法读取，请刷新相册");
    }
}
esp_err_t media_library_start(void)
{
    s_lock=xSemaphoreCreateMutex();
    if (DEVICE_CAMERA_AVAILABLE) s_photo_jobs=xQueueCreate(1,sizeof(photo_job_t));
    if (!s_lock || (DEVICE_CAMERA_AVAILABLE && !s_photo_jobs)) return ESP_ERR_NO_MEM;
    atomic_store(&s_refresh,true);
    return xTaskCreate(library_task,"media_files",6144,NULL,2,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
