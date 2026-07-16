#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Ble::Dlna {

struct DlnaTrack;

class DlnaParser {
 public:
  static bool parse_metadata(const std::string& xml, std::string& out_title, std::string& out_artist, std::string& out_art_url);

  static bool parse_volume(const std::string& xml, uint8_t& out_volume);
  static bool parse_state(const std::string& xml, std::string& out_state);
  static bool parse_position_info(const std::string& xml, uint32_t& out_rel_ms, uint32_t& out_dur_ms);
  static bool parse_next_uri(const std::string& xml, std::string& out_next_uri);

  // 오픈홈 플레이리스트 XML 파서 추가
  static bool parse_id_array(const std::string& xml, uint32_t& out_token, std::string& out_id_list);
  static bool parse_playlist_tracks(const std::string& xml, std::vector<DlnaTrack>& out_tracks);
};

}  // namespace Ble::Dlna
