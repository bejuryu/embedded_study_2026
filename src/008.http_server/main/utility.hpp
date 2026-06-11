#pragma once
#include <string>

#include "esp_err.h"

void example_uri_decode(char* dest, unsigned char* src, size_t len);
size_t get_sd_card_block_size(const std::string& mount_point);
std::string get_sanitized_hostname(const std::string& raw_hostname);
esp_err_t start_mdns_service(const std::string& hostname, uint16_t http_port, uint16_t https_port);