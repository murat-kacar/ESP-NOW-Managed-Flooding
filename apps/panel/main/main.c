#include "esp_err.h"
#include "esp_log.h"

#include "db_net.h"

static const char *TAG = "panel";

void app_main(void)
{
    ESP_ERROR_CHECK(db_net_init());
    ESP_LOGI(TAG, "started");
}
