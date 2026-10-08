#ifndef HOME_PUSH_H
#define HOME_PUSH_H
#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"
#include "home_types.h"

/* Values the homeserver pushes to POST /api/home. Pure: no I/O, no clock reads, no locks. */
typedef enum { HOME_PUSH_NONE = 0, HOME_PUSH_FRESH, HOME_PUSH_STALE, HOME_PUSH_GONE } home_push_age_t;
enum { HOME_PUSH_BATTERY = 0, HOME_PUSH_LINES_SECTION = 1 };
/* A section is stale after its ttl (home_stale_min without one) and gone after 24 hours. */
#define HOME_PUSH_GONE_S (24 * 3600)
home_push_age_t home_push_age(const home_home_t *h, int section, const home_config_t *cfg,
                              int64_t now);
/* Merges a decoded body into *h, which is left untouched when the body is refused: a present section
 * replaces the stored one, an absent one is kept, null clears it. Unknown keys and out-of-range
 * values are refused with a short reason in err. */
bool home_push_apply(const cJSON *body, home_home_t *h, int64_t now, char err[64]);
/* Whether the picture should be redrawn because of this push: the lines changed, the percent moved
 * 10 points or crossed home_low_pct, or a section went from missing or stale to fresh. */
bool home_push_redraw(const home_home_t *before, const home_home_t *after, const home_config_t *cfg,
                      int64_t now);
/* Forces strings loaded from flash to end where they should. */
void home_push_sanitise(home_home_t *h);
#endif
