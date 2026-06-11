#include "embedded_data.h"

#include "predefine.h"

const char index_html[] = {
#embed INDEX_HTML_PATH
    , '\0'};
const size_t index_html_len = sizeof(index_html) - 1;

const uint8_t https_server_cert[] = {
#embed HTTPS_CERT_PATH

    , '\0'};
const size_t https_server_cert_len = sizeof(https_server_cert) - 1;

const uint8_t https_server_key[] = {
#embed HTTPS_KEY_PATH

    , '\0'};
const size_t https_server_key_len = sizeof(https_server_key) - 1;