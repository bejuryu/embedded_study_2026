#include "dlna_parser.hpp"

#include <algorithm>

#include "dlna_constants.hpp"
#include "dlna_controller.hpp"
#include "esp_log.h"

namespace Ble::Dlna {

namespace {
static const char* TAG = "DlnaParser";

uint32_t parse_time_to_ms(const std::string& time_str) {
  int   hh = 0, mm = 0;
  float ss = 0.0f;
  if (sscanf(time_str.c_str(), "%d:%d:%f", &hh, &mm, &ss) == 3) {
    return (hh * Config::kSecondsPerHour + mm * Config::kSecondsPerMinute) * Config::kMillisecondsPerSecond + static_cast<uint32_t>(ss * Config::kMillisecondsPerSecond);
  }
  return 0;
}

std::string unescape_xml_entities(std::string str) {
  auto replace_all = [&](const std::string& from, const std::string& to) {
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
      str.replace(start_pos, from.length(), to);
      start_pos += to.length();
    }
  };
  replace_all("&amp;", "&");
  replace_all("&lt;", "<");
  replace_all("&gt;", ">");
  replace_all("&quot;", "\"");
  replace_all("&apos;", "'");
  return str;
}

std::string prepare_xml_for_parsing(const std::string& xml) {
  std::string current = xml;
  std::string last;
  int         iterations = 0;

  do {
    last    = current;
    current = unescape_xml_entities(last);
    iterations++;
  } while (current != last && iterations < 4);

  if (iterations > 1) {
    ESP_LOGD(TAG, "XML unescaped in %d passes", iterations);
  }
  return current;
}
}  // namespace

bool DlnaParser::parse_metadata(const std::string& xml, std::string& out_title, std::string& out_artist, std::string& out_art_url) {
  out_title.clear();
  out_artist.clear();
  out_art_url.clear();

  ESP_LOGD(TAG, "Parsing metadata... Raw XML Length: %d", xml.length());
  std::string decoded_xml = prepare_xml_for_parsing(xml);

  // ──────────────────────────────────────────
  // 1. TITLE PARSING (dc:title -> title)
  // ──────────────────────────────────────────
  size_t start = decoded_xml.find("<dc:title>");
  if (start != std::string::npos) {
    size_t end = decoded_xml.find("</dc:title>", start);
    if (end != std::string::npos) {
      out_title = decoded_xml.substr(start + 10, end - (start + 10));
    }
  }
  if (out_title.empty()) {
    start = decoded_xml.find("<title>");
    if (start != std::string::npos) {
      size_t end = decoded_xml.find("</title>", start);
      if (end != std::string::npos) {
        out_title = decoded_xml.substr(start + 7, end - (start + 7));
      }
    }
  }

  // ──────────────────────────────────────────
  // 2. ARTIST PARSING (upnp:artist -> artist -> dc:creator -> creator)
  // ──────────────────────────────────────────
  start = decoded_xml.find("<upnp:artist>");
  if (start != std::string::npos) {
    size_t end = decoded_xml.find("</upnp:artist>", start);
    if (end != std::string::npos) {
      out_artist = decoded_xml.substr(start + 13, end - (start + 13));
    }
  }
  if (out_artist.empty()) {
    start = decoded_xml.find("<artist>");
    if (start != std::string::npos) {
      size_t end = decoded_xml.find("</artist>", start);
      if (end != std::string::npos) {
        out_artist = decoded_xml.substr(start + 8, end - (start + 8));
      }
    }
  }
  if (out_artist.empty()) {
    start = decoded_xml.find("<dc:creator>");
    if (start != std::string::npos) {
      size_t end = decoded_xml.find("</dc:creator>", start);
      if (end != std::string::npos) {
        out_artist = decoded_xml.substr(start + 12, end - (start + 12));
      }
    }
  }
  if (out_artist.empty()) {
    start = decoded_xml.find("<creator>");
    if (start != std::string::npos) {
      size_t end = decoded_xml.find("</creator>", start);
      if (end != std::string::npos) {
        out_artist = decoded_xml.substr(start + 9, end - (start + 9));
      }
    }
  }

  // ──────────────────────────────────────────
  // 3. ALBUM ART PARSING (upnp:albumArtURI -> albumArtURI -> upnp:albumArt -> albumArt)
  // ──────────────────────────────────────────
  start = decoded_xml.find("<upnp:albumArtURI>");
  if (start != std::string::npos) {
    size_t end = decoded_xml.find("</upnp:albumArtURI>", start);
    if (end != std::string::npos) {
      out_art_url = decoded_xml.substr(start + 18, end - (start + 18));
    }
  }
  if (out_art_url.empty()) {
    start = decoded_xml.find("<upnp:albumArtURI");
    if (start != std::string::npos) {
      size_t val_start = decoded_xml.find(">", start);
      size_t end       = decoded_xml.find("</upnp:albumArtURI>", start);
      if (val_start != std::string::npos && end != std::string::npos && val_start < end) {
        out_art_url = decoded_xml.substr(val_start + 1, end - (val_start + 1));
      }
    }
  }
  if (out_art_url.empty()) {
    start = decoded_xml.find("<albumArtURI>");
    if (start != std::string::npos) {
      size_t end = decoded_xml.find("</albumArtURI>", start);
      if (end != std::string::npos) {
        out_art_url = decoded_xml.substr(start + 13, end - (start + 13));
      }
    }
  }
  if (out_art_url.empty()) {
    start = decoded_xml.find("<albumArt>");
    if (start != std::string::npos) {
      size_t end = decoded_xml.find("</albumArt>", start);
      if (end != std::string::npos) {
        out_art_url = decoded_xml.substr(start + 10, end - (start + 10));
      }
    }
  }

  ESP_LOGI(TAG, "Parsed Metadata: Title: '%s', Artist: '%s', ArtURL: '%s'", out_title.c_str(), out_artist.c_str(), out_art_url.c_str());

  return (!out_title.empty());
}

bool DlnaParser::parse_volume(const std::string& xml, uint8_t& out_volume) {
  std::string decoded_xml = prepare_xml_for_parsing(xml);

  // Fallback 1: CurrentVolume tag
  size_t start = decoded_xml.find("<CurrentVolume>");
  if (start != std::string::npos) {
    size_t end = decoded_xml.find("</CurrentVolume>", start);
    if (end != std::string::npos) {
      out_volume = strtol(decoded_xml.substr(start + 15, end - (start + 15)).c_str(), nullptr, 10);
      ESP_LOGI(TAG, "Parsed volume from <CurrentVolume>: %d", out_volume);
      return true;
    }
  }

  // Fallback 2: Volume tag with val attribute
  start = decoded_xml.find("<Volume");
  if (start != std::string::npos) {
    size_t val_pos = decoded_xml.find("val=\"", start);
    if (val_pos != std::string::npos) {
      size_t end_quote = decoded_xml.find("\"", val_pos + 5);
      if (end_quote != std::string::npos) {
        out_volume = strtol(decoded_xml.substr(val_pos + 5, end_quote - (val_pos + 5)).c_str(), nullptr, 10);
        ESP_LOGI(TAG, "Parsed volume from <Volume val=...>: %d", out_volume);
        return true;
      }
    }
  }

  // Fallback 3: Plain <Volume> tag
  start = decoded_xml.find("<Volume>");
  if (start != std::string::npos) {
    size_t end = decoded_xml.find("</Volume>", start);
    if (end != std::string::npos) {
      out_volume = strtol(decoded_xml.substr(start + 8, end - (start + 8)).c_str(), nullptr, 10);
      ESP_LOGI(TAG, "Parsed volume from <Volume>: %d", out_volume);
      return true;
    }
  }

  ESP_LOGD(TAG, "Failed to parse volume from XML.");
  return false;
}

bool DlnaParser::parse_state(const std::string& xml, std::string& out_state) {
  std::string decoded_xml = prepare_xml_for_parsing(xml);
  size_t      start       = decoded_xml.find("<TransportState");
  if (start != std::string::npos) {
    size_t val_pos = decoded_xml.find("val=\"", start);
    if (val_pos != std::string::npos) {
      size_t end_quote = decoded_xml.find("\"", val_pos + 5);
      if (end_quote != std::string::npos) {
        out_state = decoded_xml.substr(val_pos + 5, end_quote - (val_pos + 5));
        return true;
      }
    }
  }

  start = decoded_xml.find("<CurrentTransportState>");
  if (start != std::string::npos) {
    size_t end = decoded_xml.find("</CurrentTransportState>", start);
    if (end != std::string::npos) {
      out_state = decoded_xml.substr(start + 23, end - (start + 23));
      return true;
    }
  }
  return false;
}

bool DlnaParser::parse_position_info(const std::string& xml, uint32_t& out_rel_ms, uint32_t& out_dur_ms) {
  out_rel_ms = 0;
  out_dur_ms = 0;

  std::string decoded_xml = prepare_xml_for_parsing(xml);

  // RelTime 및 RelativeTime 태그 호환 탐색
  size_t start = decoded_xml.find("<RelTime>");
  if (start != std::string::npos) {
    size_t end = decoded_xml.find("</RelTime>", start);
    if (end != std::string::npos) {
      std::string rel_time_str = decoded_xml.substr(start + 9, end - (start + 9));
      out_rel_ms               = parse_time_to_ms(rel_time_str);
    }
  } else {
    start = decoded_xml.find("<RelativeTime>");
    if (start != std::string::npos) {
      size_t end = decoded_xml.find("</RelativeTime>", start);
      if (end != std::string::npos) {
        std::string rel_time_str = decoded_xml.substr(start + 14, end - (start + 14));
        out_rel_ms               = parse_time_to_ms(rel_time_str);
      }
    }
  }

  start = decoded_xml.find("<TrackDuration>");
  if (start != std::string::npos) {
    size_t end = decoded_xml.find("</TrackDuration>", start);
    if (end != std::string::npos) {
      std::string dur_time_str = decoded_xml.substr(start + 15, end - (start + 15));
      out_dur_ms               = parse_time_to_ms(dur_time_str);
    }
  }

  return (out_dur_ms > 0);
}

bool DlnaParser::parse_next_uri(const std::string& xml, std::string& out_next_uri) {
  out_next_uri.clear();
  std::string decoded_xml = prepare_xml_for_parsing(xml);
  size_t      start       = decoded_xml.find("<NextURI>");
  if (start != std::string::npos) {
    size_t end = decoded_xml.find("</NextURI>", start);
    if (end != std::string::npos) {
      out_next_uri = decoded_xml.substr(start + 9, end - (start + 9));
      return true;
    }
  }

  start = decoded_xml.find("<NextURIMetaData>");
  if (start != std::string::npos) {
    size_t end = decoded_xml.find("</NextURIMetaData>", start);
    if (end != std::string::npos) {
      std::string meta = decoded_xml.substr(start + 17, end - (start + 17));
      if (!meta.empty() && meta != "NOT_IMPLEMENTED") {
        out_next_uri = "VALID_META";  // 메타데이터가 정상 채워져 있으면 유효하다고 판단
        return true;
      }
    }
  }
  return false;
}

namespace {
std::vector<uint8_t> base64_decode(const std::string& in) {
  std::vector<uint8_t> out;
  std::vector<int>     T(256, -1);
  for (int i = 0; i < 64; i++) T["ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[i]] = i;
  int val = 0, valb = -8;
  for (uint8_t c : in) {
    if (T[c] == -1) continue;
    val   = (val << 6) | T[c];
    valb += 6;
    if (valb >= 0) {
      out.push_back((val >> valb) & 0xFF);
      valb -= 8;
    }
  }
  return out;
}
}  // namespace

bool DlnaParser::parse_id_array(const std::string& xml, uint32_t& out_token, std::string& out_id_list) {
  out_token = 0;
  out_id_list.clear();
  std::string decoded_xml = prepare_xml_for_parsing(xml);

  size_t t_start = decoded_xml.find("<Token>");
  if (t_start != std::string::npos) {
    size_t t_end = decoded_xml.find("</Token>", t_start);
    if (t_end != std::string::npos) {
      out_token = std::stoul(decoded_xml.substr(t_start + 7, t_end - (t_start + 7)));
    }
  }

  size_t a_start = decoded_xml.find("<Array>");
  if (a_start != std::string::npos) {
    size_t a_end = decoded_xml.find("</Array>", a_start);
    if (a_end != std::string::npos) {
      std::string base64_data = decoded_xml.substr(a_start + 7, a_end - (a_start + 7));
      // 공백이나 개행문자 제거
      base64_data.erase(std::remove_if(base64_data.begin(), base64_data.end(), [](unsigned char x) { return std::isspace(x); }), base64_data.end());

      std::vector<uint8_t> bin = base64_decode(base64_data);
      std::string          id_list;
      for (size_t i = 0; i + 3 < bin.size(); i += 4) {
        uint32_t id = (bin[i] << 24) | (bin[i + 1] << 16) | (bin[i + 2] << 8) | bin[i + 3];
        if (!id_list.empty()) id_list += " ";
        id_list += std::to_string(id);
      }
      out_id_list = id_list;
      return true;
    }
  }
  return false;
}

bool DlnaParser::parse_playlist_tracks(const std::string& xml, std::vector<DlnaTrack>& out_tracks) {
  out_tracks.clear();
  std::string decoded_xml = prepare_xml_for_parsing(xml);

  size_t track_pos = 0;
  while ((track_pos = decoded_xml.find("<Track>", track_pos)) != std::string::npos) {
    size_t track_end = decoded_xml.find("</Track>", track_pos);
    if (track_end == std::string::npos) break;

    std::string track_block = decoded_xml.substr(track_pos, track_end - track_pos);

    uint32_t id       = 0;
    size_t   id_start = track_block.find("<Id>");
    if (id_start != std::string::npos) {
      size_t id_end = track_block.find("</Id>", id_start);
      if (id_end != std::string::npos) {
        id = std::stoul(track_block.substr(id_start + 4, id_end - (id_start + 4)));
      }
    }

    std::string title, artist, art_url;
    size_t      meta_start = track_block.find("<Metadata>");
    if (meta_start != std::string::npos) {
      size_t meta_end = track_block.find("</Metadata>", meta_start);
      if (meta_end != std::string::npos) {
        std::string metadata_xml = track_block.substr(meta_start + 10, meta_end - (meta_start + 10));
        parse_metadata(metadata_xml, title, artist, art_url);
      }
    }

    if (!title.empty()) {
      out_tracks.push_back(DlnaTrack{.id = id, .title = title, .artist = artist, .art_url = art_url});
    }

    track_pos = track_end + 8;
  }
  return !out_tracks.empty();
}

}  // namespace Ble::Dlna
