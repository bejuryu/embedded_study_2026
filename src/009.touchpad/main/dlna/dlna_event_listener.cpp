#include "dlna_event_listener.hpp"

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <string>

#include "dlna_constants.hpp"
#include "esp_log.h"

namespace Ble::Dlna {

static const char* TAG = "DlnaGenaListener";

DlnaEventListener::DlnaEventListener(QueueHandle_t event_queue) : event_queue_(event_queue) {}

DlnaEventListener::~DlnaEventListener() { stop(); }

bool DlnaEventListener::start() {
  if (is_running_) return true;
  is_running_ = true;

  xTaskCreate(listener_thread, "gena_http_listener", 4096, this, 5, &listener_task_handle_);
  return (listener_task_handle_ != nullptr);
}

void DlnaEventListener::stop() {
  is_running_ = false;
  if (server_fd_ != -1) {
    close(server_fd_);
    server_fd_ = -1;
  }
  listener_task_handle_ = nullptr;
}

void DlnaEventListener::listener_thread(void* arg) {
  auto* self = static_cast<DlnaEventListener*>(arg);

  self->server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (self->server_fd_ < 0) {
    ESP_LOGE(TAG, "Failed to create socket");
    vTaskDelete(nullptr);
    return;
  }

  int opt = 1;
  setsockopt(self->server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr{};
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port        = htons(Ble::Dlna::Config::kGenaListenerPort);  // 8080

  if (bind(self->server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    ESP_LOGE(TAG, "Socket bind failed (Port: %d)", Ble::Dlna::Config::kGenaListenerPort);
    close(self->server_fd_);
    self->server_fd_ = -1;
    vTaskDelete(nullptr);
    return;
  }

  if (listen(self->server_fd_, 2) < 0) {  // Backlog = 2
    ESP_LOGE(TAG, "Socket listen failed");
    close(self->server_fd_);
    self->server_fd_ = -1;
    vTaskDelete(nullptr);
    return;
  }

  ESP_LOGI(TAG, "GENA HTTP Listener started on port %d", Ble::Dlna::Config::kGenaListenerPort);

  struct timeval timeout{};
  timeout.tv_sec  = 0;
  timeout.tv_usec = 500000;  // 500ms

  while (self->is_running_) {
    struct sockaddr_in client_addr{};
    socklen_t          client_len = sizeof(client_addr);
    int                client_fd  = accept(self->server_fd_, (struct sockaddr*)&client_addr, &client_len);

    if (client_fd < 0) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    // 500ms 수신/송신 타임아웃 세팅 (소켓 고갈 및 프리징 차단)
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    std::string raw_request;
    char        buf[512];

    // 간단한 HTTP 헤더/바디 읽기 루프
    while (self->is_running_) {
      int ret = recv(client_fd, buf, sizeof(buf) - 1, 0);
      if (ret <= 0) break;
      buf[ret] = '\0';
      raw_request.append(buf, ret);

      // HTTP header가 끝났고 content-length만큼 수신했는지 확인
      size_t header_end = raw_request.find("\r\n\r\n");
      if (header_end != std::string::npos) {
        size_t cl_pos = raw_request.find("Content-Length:");
        if (cl_pos == std::string::npos) {
          cl_pos = raw_request.find("content-length:");
        }

        if (cl_pos != std::string::npos && cl_pos < header_end) {
          size_t cl_val_pos = raw_request.find_first_of("0123456789", cl_pos);
          if (cl_val_pos != std::string::npos && cl_val_pos < header_end) {
            int    cl_val        = std::stoi(raw_request.substr(cl_val_pos));
            size_t body_received = raw_request.length() - (header_end + 4);
            if (body_received >= static_cast<size_t>(cl_val)) {
              break;  // 수신 완료
            }
          }
        } else {
          break;
        }
      }
    }

    if (!raw_request.empty() && raw_request.find("NOTIFY") != std::string::npos) {
      size_t xml_start = raw_request.find("<e:propertyset");
      if (xml_start == std::string::npos) {
        xml_start = raw_request.find("<propertyset");
      }

      if (xml_start != std::string::npos) {
        std::string xml_body = raw_request.substr(xml_start);
        // 큐에 복사하여 비동기 전달 (메모리 이중 해제 방지 포인터 전송)
        auto*       p_xml    = new std::string(xml_body);
        if (xQueueSend(self->event_queue_, &p_xml, 0) != pdTRUE) {
          delete p_xml;
        }
      }

      // HTTP 200 OK 리턴
      const char* response =
          "HTTP/1.1 200 OK\r\n"
          "Content-Length: 0\r\n"
          "Connection: close\r\n\r\n";
      send(client_fd, response, strlen(response), 0);
    }

    close(client_fd);
  }

  close(self->server_fd_);
  self->server_fd_ = -1;
  vTaskDelete(nullptr);
}

}  // namespace Ble::Dlna
