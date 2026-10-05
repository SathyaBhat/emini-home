#include "home_picture.h"
#include "home_runtime.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdlib.h>
#include <string.h>

/* Two slots at the start of the "assets" partition, which Home otherwise leaves alone: it is the
 * stock firmware's photo gallery, and whatever it held there is overwritten by the first picture
 * (a full flash backup brings it back). A new picture goes into the slot not in use and its header
 * is written last, so a power cut during an upload leaves the previous picture on the screen.
 * Only these 64 KiB are ever erased; the rest of the partition is not touched. */
#define PICTURE_MAGIC 0x50494331U /* "PIC1" */
#define PICTURE_SLOT 0x8000U
#define PICTURE_DATA 64U
typedef struct {
    uint32_t magic, sequence, size;
    uint8_t hash[32];
} header_t;
_Static_assert(sizeof(header_t) <= PICTURE_DATA, "header overlaps the frame");
_Static_assert(PICTURE_DATA + HOME_FRAME_BYTES <= PICTURE_SLOT, "frame does not fit a slot");

static const char *TAG = "home_picture";
static const esp_partition_t *part;
static SemaphoreHandle_t lock;
/* Slot holding the newest valid picture. Only ever goes from -1 to a slot, and an int is read in
 * one go, so home_picture_present() can read it without the lock: it is asked by the button
 * handler under home_lock(), which must not wait for an upload erasing flash. */
static volatile int current = -1;
static uint32_t sequence;

static bool slot_valid(int slot, header_t *h, uint8_t *frame)
{
    if (esp_partition_read(part, slot * PICTURE_SLOT, h, sizeof(*h)) != ESP_OK ||
        h->magic != PICTURE_MAGIC || h->size != HOME_FRAME_BYTES || !h->sequence ||
        h->sequence == UINT32_MAX)
        return false;
    if (esp_partition_read(part, slot * PICTURE_SLOT + PICTURE_DATA, frame, HOME_FRAME_BYTES) !=
        ESP_OK)
        return false;
    uint8_t hash[32];
    return home_hash(frame, HOME_FRAME_BYTES, hash) && !memcmp(hash, h->hash, sizeof(hash));
}
esp_err_t home_picture_init(void)
{
    lock = xSemaphoreCreateMutex();
    if (!lock)
        return ESP_ERR_NO_MEM;
    part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
                                    "assets");
    if (!part || part->size < 2 * PICTURE_SLOT) {
        part = NULL;
        ESP_LOGW(TAG, "No assets partition; Picture keeps nothing");
        return ESP_ERR_NOT_FOUND;
    }
    uint8_t *frame = malloc(HOME_FRAME_BYTES);
    if (!frame)
        return ESP_ERR_NO_MEM;
    for (int slot = 0; slot < 2; slot++) {
        header_t h;
        if (slot_valid(slot, &h, frame) && h.sequence > sequence) {
            sequence = h.sequence;
            current = slot;
        }
    }
    free(frame);
    return ESP_OK;
}
bool home_picture_present(void)
{
    return current >= 0;
}
bool home_picture_load(uint8_t frame[HOME_FRAME_BYTES])
{
    if (!lock)
        return false;
    xSemaphoreTake(lock, portMAX_DELAY);
    header_t h;
    bool ok = current >= 0 && slot_valid(current, &h, frame);
    xSemaphoreGive(lock);
    return ok;
}
esp_err_t home_picture_store(const uint8_t frame[HOME_FRAME_BYTES])
{
    if (!lock || !part)
        return ESP_ERR_NOT_FOUND;
    header_t h = {PICTURE_MAGIC, 0, HOME_FRAME_BYTES, {0}};
    if (!home_hash(frame, HOME_FRAME_BYTES, h.hash))
        return ESP_FAIL;
    xSemaphoreTake(lock, portMAX_DELAY);
    int slot = current == 0 ? 1 : 0;
    h.sequence = sequence + 1;
    esp_err_t e = h.sequence == UINT32_MAX ? ESP_ERR_INVALID_STATE : ESP_OK;
    if (e == ESP_OK)
        e = esp_partition_erase_range(part, slot * PICTURE_SLOT, PICTURE_SLOT);
    if (e == ESP_OK)
        e = esp_partition_write(part, slot * PICTURE_SLOT + PICTURE_DATA, frame, HOME_FRAME_BYTES);
    if (e == ESP_OK)
        e = esp_partition_write(part, slot * PICTURE_SLOT, &h, sizeof(h));
    if (e == ESP_OK) {
        current = slot;
        sequence = h.sequence;
    }
    xSemaphoreGive(lock);
    if (e != ESP_OK)
        ESP_LOGW(TAG, "Picture not saved: %s", esp_err_to_name(e));
    return e;
}
