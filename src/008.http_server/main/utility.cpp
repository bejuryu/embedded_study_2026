#include "utility.hpp"

#include <sys/statvfs.h>
#include <sys/types.h>

#include <algorithm>
#include <cctype>

#include "esp_log.h"
#include "mdns.h"

// https://github.com/espressif/esp-idf/blob/release/v6.1/examples/common_components/protocol_examples_common/protocol_examples_utils.c

/* Type of Escape algorithms to be used */
#define NGX_ESCAPE_URI (0)
#define NGX_ESCAPE_ARGS (1)
#define NGX_ESCAPE_URI_COMPONENT (2)
#define NGX_ESCAPE_HTML (3)
#define NGX_ESCAPE_REFRESH (4)
#define NGX_ESCAPE_MEMCACHED (5)
#define NGX_ESCAPE_MAIL_AUTH (6)

/* Type of Unescape algorithms to be used */
#define NGX_UNESCAPE_URI (1)
#define NGX_UNESCAPE_REDIRECT (2)

void ngx_unescape_uri(u_char** dst, u_char** src, size_t size, unsigned int type) {
  u_char ch, c;
  u_char* d = *dst;
  u_char* s = *src;

  enum { sw_usual = 0, sw_quoted, sw_quoted_second } state = sw_usual;
  u_char decoded = 0;

  while (size--) {
    ch = *s++;
    switch (state) {
      case sw_usual:
        if (ch == '?' && (type & (NGX_UNESCAPE_URI | NGX_UNESCAPE_REDIRECT))) {
          *d++ = ch;
          goto done;
        }
        if (ch == '%') {
          state = sw_quoted;
          break;
        }
        *d++ = ch;
        break;
      case sw_quoted:
        if (ch >= '0' && ch <= '9') {
          decoded = static_cast<u_char>(ch - '0');
          state = sw_quoted_second;
          break;
        }
        c = static_cast<u_char>(ch | 0x20);
        if (c >= 'a' && c <= 'f') {
          decoded = static_cast<u_char>(c - 'a' + 10);
          state = sw_quoted_second;
          break;
        }
        /* the invalid quoted character */
        state = sw_usual;
        *d++ = ch;
        break;
      case sw_quoted_second:
        state = sw_usual;
        if (ch >= '0' && ch <= '9') {
          ch = static_cast<u_char>((decoded << 4) + (ch - '0'));
          if (type & NGX_UNESCAPE_REDIRECT) {
            if (ch > '%' && ch < 0x7f) {
              *d++ = ch;
              break;
            }
            *d++ = '%';
            *d++ = *(s - 2);
            *d++ = *(s - 1);
            break;
          }
          *d++ = ch;
          break;
        }

        c = static_cast<u_char>(ch | 0x20);
        if (c >= 'a' && c <= 'f') {
          ch = static_cast<u_char>((decoded << 4) + (c - 'a') + 10);
          if (type & NGX_UNESCAPE_URI) {
            if (ch == '?') {
              *d++ = ch;
              goto done;
            }
            *d++ = ch;
            break;
          }
          if (type & NGX_UNESCAPE_REDIRECT) {
            if (ch == '?') {
              *d++ = ch;
              goto done;
            }
            if (ch > '%' && ch < 0x7f) {
              *d++ = ch;
              break;
            }
            *d++ = '%';
            *d++ = *(s - 2);
            *d++ = *(s - 1);
            break;
          }
          *d++ = ch;
          break;
        }
        /* the invalid quoted character */
        break;
    }
  }

done:
  *dst = d;
  *src = s;
}

void example_uri_decode(char* dest, unsigned char* src, size_t len) {
  if (!src || !dest) return;
  ngx_unescape_uri(reinterpret_cast<u_char**>(&dest), &src, len, NGX_UNESCAPE_URI);
}

size_t get_sd_card_block_size(const std::string& mount_point) {
  struct statvfs stat{};
  if (statvfs(mount_point.c_str(), &stat) == 0 && stat.f_bsize > 0) {
    return std::clamp(static_cast<size_t>(stat.f_bsize), static_cast<size_t>(4096), static_cast<size_t>(32768));
  }
  return 4096;
}

std::string get_sanitized_hostname(const std::string& raw_hostname) {
  std::string sanitized = raw_hostname;
  std::string suffix = ".local";
  if (sanitized.size() >= suffix.size()) {
    std::string tail = sanitized.substr(sanitized.size() - suffix.size());
    std::transform(tail.begin(), tail.end(), tail.begin(), [](unsigned char c) { return std::tolower(c); });
    if (tail == suffix) {
      sanitized.erase(sanitized.size() - suffix.size());
    }
  }
  return sanitized;
}

esp_err_t start_mdns_service(const std::string& hostname, uint16_t http_port, uint16_t https_port) {
  esp_err_t err = mdns_init();
  if (err != ESP_OK) {
    ESP_LOGE("utility", "Failed to initialize mDNS: %s", esp_err_to_name(err));
    return err;
  }

  err = mdns_hostname_set(hostname.c_str());
  if (err != ESP_OK) {
    ESP_LOGE("utility", "Failed to set mDNS hostname: %s", esp_err_to_name(err));
    return err;
  }

  err = mdns_instance_name_set("M5Stack Tab5 Control Panel");
  if (err != ESP_OK) {
    ESP_LOGE("utility", "Failed to set mDNS instance name: %s", esp_err_to_name(err));
    return err;
  }

  if (http_port > 0) {
    mdns_service_add(nullptr, "_http", "_tcp", http_port, nullptr, 0);
  }
  if (https_port > 0) {
    mdns_service_add(nullptr, "_https", "_tcp", https_port, nullptr, 0);
  }

  ESP_LOGI("utility", "mDNS responder started with hostname: %s.local", hostname.c_str());
  return ESP_OK;
}