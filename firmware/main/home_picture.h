#ifndef HOME_PICTURE_H
#define HOME_PICTURE_H
#include "home_types.h"
#include "esp_err.h"
#include <stdbool.h>
/* The Picture screen's frame, kept on flash so it survives a restart. Packed exactly like
 * home_render()'s output (B0/W1/Y2/R3, four pixels MSB first) and shown as it is. */
esp_err_t home_picture_init(void);
bool home_picture_present(void);
/* Fills frame and returns true, or leaves it untouched and returns false. */
bool home_picture_load(uint8_t frame[HOME_FRAME_BYTES]);
esp_err_t home_picture_store(const uint8_t frame[HOME_FRAME_BYTES]);
#endif
