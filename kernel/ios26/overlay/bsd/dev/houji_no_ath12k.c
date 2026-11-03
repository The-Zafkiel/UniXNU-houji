/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Build without dada's external ath12k object. The iOS16 houji baseline has
 * no working Wi-Fi backend; do not bind SM8750 firmware or report a radio.
 * Link this file only when the external nw_wifi implementation is absent.
 */
#include <sys/types.h>
#include <sys/errno.h>
#include <libkern/libkern.h>

struct lx_platform;
struct nw_board;
struct nw_wifi_events;
struct nw_bss_info;
struct nw_wifi_stats {
    uint32_t stage, scans, frames, beacons, bss_seen;
    int last_err;
};
struct nw_wifi_status {
    uint32_t state;
    int32_t result;
    uint8_t bssid[6], ssid_len, ssid[32];
    uint32_t freq_mhz;
    int8_t signal_dbm;
    uint8_t mac[6];
    uint32_t channels;
};

struct nw_wifi_stats nw_wifi_stats;
volatile uint64_t nw_wifi_where[4];

int nw_wifi_start(const struct lx_platform *platform, const struct nw_board *board)
{
    (void)platform;
    (void)board;
    nw_wifi_stats.last_err = -ENODEV;
    return -ENODEV;
}
int nw_wifi_step(void) { return -ENODEV; }
void nw_wifi_stop(void) {}
void nw_wifi_set_events(const struct nw_wifi_events *events) { (void)events; }
int nw_wifi_scan(const uint8_t *ssid, size_t len)
{
    (void)ssid; (void)len;
    return -ENODEV;
}
int nw_wifi_bss_count(void) { return 0; }
int nw_wifi_bss_get(int index, struct nw_bss_info *out)
{
    (void)index; (void)out;
    return -ENODEV;
}
int nw_wifi_connect(const uint8_t *ssid, size_t len, const uint8_t *bssid,
    const char *password, size_t password_len)
{
    (void)ssid; (void)len; (void)bssid; (void)password; (void)password_len;
    return -ENODEV;
}
void nw_wifi_disconnect(void) {}
int nw_wifi_tx(const uint8_t *frame, size_t len)
{
    (void)frame; (void)len;
    return -ENODEV;
}
void nw_wifi_status_get(struct nw_wifi_status *status)
{
    if (status != NULL) {
        bzero(status, sizeof(*status));
        status->result = 5; /* NW_RES_UNSUPPORTED; NW_STA_OFF remains zero. */
    }
}
