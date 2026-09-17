#include "store.h"

#include <stdio.h>
#include <string.h>

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_littlefs.h"
#include "esp_log.h"

static const char *TAG = "store";

#define STORE_NS       "deskink"
#define STORE_FS_BASE  "/littlefs"
#define STORE_PART     "littlefs"

static nvs_handle_t s_nvs;
static bool         s_nvs_ready;
static bool         s_fs_ready;

esp_err_t store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_open(STORE_NS, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }
    s_nvs_ready = true;

    const esp_vfs_littlefs_conf_t conf = {
        .base_path = STORE_FS_BASE,
        .partition_label = STORE_PART,
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        /* 文件系统挂了不阻断启动，NVS 仍可用 */
        ESP_LOGE(TAG, "LittleFS mount failed: %s", esp_err_to_name(err));
        s_fs_ready = false;
    } else {
        size_t total = 0, used = 0;
        esp_littlefs_info(conf.partition_label, &total, &used);
        ESP_LOGI(TAG, "LittleFS mounted: %u/%u bytes used", (unsigned)used, (unsigned)total);
        s_fs_ready = true;
    }

    ESP_LOGI(TAG, "NVS ready (namespace '%s')", STORE_NS);
    return ESP_OK;
}

/* ---------- NVS ---------- */

esp_err_t store_get_u32(const char *key, uint32_t *out)
{
    if (!s_nvs_ready || key == NULL || out == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return nvs_get_u32(s_nvs, key, out);
}

esp_err_t store_set_u32(const char *key, uint32_t val)
{
    if (!s_nvs_ready || key == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return nvs_set_u32(s_nvs, key, val);
}

esp_err_t store_get_str(const char *key, char *buf, size_t len)
{
    if (!s_nvs_ready || key == NULL || buf == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return nvs_get_str(s_nvs, key, buf, &len);
}

esp_err_t store_set_str(const char *key, const char *val)
{
    if (!s_nvs_ready || key == NULL || val == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return nvs_set_str(s_nvs, key, val);
}

/* ---------- LittleFS ---------- */

static esp_err_t fs_path(const char *path, char *out, size_t cap)
{
    if (path == NULL || path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    int n = snprintf(out, cap, "%s/%s", STORE_FS_BASE, path);
    return (n > 0 && (size_t)n < cap) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t store_write_file(const char *path, const void *data, size_t len)
{
    if (!s_fs_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    char full[128];
    esp_err_t err = fs_path(path, full, sizeof(full));
    if (err != ESP_OK) {
        return err;
    }
    FILE *f = fopen(full, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "open '%s' for write failed", full);
        return ESP_FAIL;
    }
    size_t w = fwrite(data, 1, len, f);
    fclose(f);
    return (w == len) ? ESP_OK : ESP_FAIL;
}

esp_err_t store_read_file(const char *path, void *buf, size_t cap, size_t *out_len)
{
    if (!s_fs_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    char full[128];
    esp_err_t err = fs_path(path, full, sizeof(full));
    if (err != ESP_OK) {
        return err;
    }
    FILE *f = fopen(full, "rb");
    if (f == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    size_t r = fread(buf, 1, cap, f);
    fclose(f);
    if (out_len) {
        *out_len = r;
    }
    return ESP_OK;
}

esp_err_t store_delete_file(const char *path)
{
    if (!s_fs_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    char full[128];
    esp_err_t err = fs_path(path, full, sizeof(full));
    if (err != ESP_OK) {
        return err;
    }
    return (remove(full) == 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}
