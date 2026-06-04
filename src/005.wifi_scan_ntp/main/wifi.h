#pragma once

#include "esp_wifi.h"

struct custom_wifi_info_t {
  char* ssid;
  char* password;
};

bool custom_wifi_info_init(struct custom_wifi_info_t* wifi_info);
void custom_wifi_info_free(struct custom_wifi_info_t* wifi_info);

bool load_wifi_info(const char* load_path, struct custom_wifi_info_t* wifi_info);

void wifi_init_station();

wifi_ap_record_t* wifi_scan(size_t* results_count);
bool wifi_exists_ap(const char* ssid, const wifi_ap_record_t* scan_result, size_t scan_count);
bool wifi_connect(const char* ssid, const char* password);
bool wifi_sntp_sync(const char* sntp_server);