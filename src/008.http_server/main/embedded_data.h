#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const char index_html[];
extern const size_t index_html_len;
extern const uint8_t https_server_cert[];
extern const size_t https_server_cert_len;
extern const uint8_t https_server_key[];
extern const size_t https_server_key_len;

#ifdef __cplusplus
}
#endif