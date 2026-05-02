#include "snmp_sensor.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sstream>

namespace esphome {
namespace snmp_sensor {

static const char *const TAG = "snmp_sensor";

static void enc_len(std::vector<uint8_t> &v, size_t n) {
  if (n < 128) {
    v.push_back((uint8_t) n);
  } else if (n < 256) {
    v.push_back(0x81);
    v.push_back((uint8_t) n);
  } else {
    v.push_back(0x82);
    v.push_back((uint8_t) (n >> 8));
    v.push_back((uint8_t) (n & 0xff));
  }
}

static void enc_oid(std::vector<uint8_t> &out, const std::string &oid) {
  std::vector<uint32_t> parts;
  std::stringstream ss(oid);
  std::string tok;
  while (std::getline(ss, tok, '.')) {
    if (!tok.empty())
      parts.push_back((uint32_t) std::stoul(tok));
  }
  if (parts.size() < 2)
    return;

  std::vector<uint8_t> body;
  auto push_subid = [&](uint32_t n) {
    uint8_t tmp[5];
    int i = 0;
    tmp[i++] = n & 0x7f;
    n >>= 7;
    while (n > 0) {
      tmp[i++] = (n & 0x7f) | 0x80;
      n >>= 7;
    }
    for (int j = i - 1; j >= 0; --j)
      body.push_back(tmp[j]);
  };
  push_subid(parts[0] * 40 + parts[1]);
  for (size_t k = 2; k < parts.size(); ++k)
    push_subid(parts[k]);

  out.push_back(0x06);
  enc_len(out, body.size());
  out.insert(out.end(), body.begin(), body.end());
}

std::vector<uint8_t> SNMPSensor::build_packet_(uint16_t req_id) {
  std::vector<uint8_t> vb_inner;
  enc_oid(vb_inner, oid_);
  vb_inner.push_back(0x05);
  vb_inner.push_back(0x00);

  std::vector<uint8_t> vb;
  vb.push_back(0x30);
  enc_len(vb, vb_inner.size());
  vb.insert(vb.end(), vb_inner.begin(), vb_inner.end());

  std::vector<uint8_t> vbs;
  vbs.push_back(0x30);
  enc_len(vbs, vb.size());
  vbs.insert(vbs.end(), vb.begin(), vb.end());

  std::vector<uint8_t> pdu_inner;
  pdu_inner.push_back(0x02);
  pdu_inner.push_back(0x02);
  pdu_inner.push_back((uint8_t) (req_id >> 8));
  pdu_inner.push_back((uint8_t) (req_id & 0xff));
  pdu_inner.push_back(0x02);
  pdu_inner.push_back(0x01);
  pdu_inner.push_back(0x00);
  pdu_inner.push_back(0x02);
  pdu_inner.push_back(0x01);
  pdu_inner.push_back(0x00);
  pdu_inner.insert(pdu_inner.end(), vbs.begin(), vbs.end());

  std::vector<uint8_t> pdu;
  pdu.push_back(0xa0);
  enc_len(pdu, pdu_inner.size());
  pdu.insert(pdu.end(), pdu_inner.begin(), pdu_inner.end());

  std::vector<uint8_t> main_inner;
  main_inner.push_back(0x02);
  main_inner.push_back(0x01);
  main_inner.push_back(0x01);
  main_inner.push_back(0x04);
  enc_len(main_inner, community_.size());
  main_inner.insert(main_inner.end(), community_.begin(), community_.end());
  main_inner.insert(main_inner.end(), pdu.begin(), pdu.end());

  std::vector<uint8_t> out;
  out.push_back(0x30);
  enc_len(out, main_inner.size());
  out.insert(out.end(), main_inner.begin(), main_inner.end());
  return out;
}

bool SNMPSensor::parse_(const uint8_t *data, size_t len, int32_t *out_value) {
  size_t i = 0;
  while (i + 2 < len) {
    uint8_t tag = data[i++];
    if (i >= len)
      return false;
    size_t l;
    if ((data[i] & 0x80) == 0) {
      l = data[i++];
    } else {
      uint8_t nbytes = data[i++] & 0x7f;
      if (nbytes == 0 || nbytes > 4 || i + nbytes > len)
        return false;
      l = 0;
      for (uint8_t k = 0; k < nbytes; ++k)
        l = (l << 8) | data[i++];
    }
    if (tag == 0x06) {
      if (i + l >= len)
        return false;
      i += l;
      uint8_t vtag = data[i++];
      if (i >= len)
        return false;
      size_t vl;
      if ((data[i] & 0x80) == 0) {
        vl = data[i++];
      } else {
        uint8_t nbytes = data[i++] & 0x7f;
        if (nbytes == 0 || nbytes > 4 || i + nbytes > len)
          return false;
        vl = 0;
        for (uint8_t k = 0; k < nbytes; ++k)
          vl = (vl << 8) | data[i++];
      }
      // Accept INTEGER (0x02) plus application-type numeric tags:
      //   0x41 Counter32, 0x42 Gauge32, 0x43 TimeTicks, 0x46 Counter64.
      // MikroTik returns SFP+ DDM values as Gauge32.
      bool is_int = (vtag == 0x02);
      bool is_unsigned = (vtag == 0x41 || vtag == 0x42 || vtag == 0x43 || vtag == 0x46);
      if ((!is_int && !is_unsigned) || vl == 0 || vl > 5 || i + vl > len)
        return false;
      int32_t v;
      if (is_int) {
        v = (int8_t) data[i];  // sign-extend
        for (size_t k = 1; k < vl; ++k)
          v = (v << 8) | data[i + k];
      } else {
        uint32_t u = 0;
        // Skip leading zero byte if present (BER unsigned can prepend 0x00 to
        // disambiguate from negative INTEGER, e.g. Gauge32=200 -> 00 C8).
        size_t start = (vl > 1 && data[i] == 0x00) ? 1 : 0;
        for (size_t k = start; k < vl; ++k)
          u = (u << 8) | data[i + k];
        v = (int32_t) u;
      }
      *out_value = v;
      return true;
    }
    if (tag == 0x30 || (tag & 0xe0) == 0xa0) {
      continue;
    }
    i += l;
  }
  return false;
}

void SNMPSensor::update() {
  if (waiting_) {
    ESP_LOGW(TAG, "previous request still pending; abandoning");
    if (sock_ >= 0) {
      ::close(sock_);
      sock_ = -1;
    }
    waiting_ = false;
  }

  sock_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock_ < 0) {
    ESP_LOGW(TAG, "socket() failed: errno=%d", errno);
    return;
  }
  int flags = ::fcntl(sock_, F_GETFL, 0);
  ::fcntl(sock_, F_SETFL, flags | O_NONBLOCK);

  struct sockaddr_in dest{};
  dest.sin_family = AF_INET;
  dest.sin_port = htons(port_);
  if (::inet_aton(host_.c_str(), &dest.sin_addr) == 0) {
    ESP_LOGW(TAG, "invalid host '%s'", host_.c_str());
    ::close(sock_);
    sock_ = -1;
    return;
  }

  auto pkt = build_packet_(++req_id_);
  int n = ::sendto(sock_, pkt.data(), pkt.size(), 0, (struct sockaddr *) &dest, sizeof(dest));
  if (n < 0) {
    ESP_LOGW(TAG, "sendto() failed: errno=%d", errno);
    ::close(sock_);
    sock_ = -1;
    return;
  }
  ESP_LOGD(TAG, "sent %d bytes to %s:%u (req_id=%u, oid=%s)", n, host_.c_str(), port_, req_id_, oid_.c_str());
  send_time_ms_ = millis();
  waiting_ = true;
}

void SNMPSensor::loop() {
  if (!waiting_ || sock_ < 0)
    return;
  uint8_t buf[256];
  int n = ::recv(sock_, buf, sizeof(buf), 0);
  if (n > 0) {
    ESP_LOGD(TAG, "received %d bytes", n);
    int32_t value = 0;
    if (parse_(buf, (size_t) n, &value)) {
      this->publish_state((float) value);
      ESP_LOGD(TAG, "value=%d", (int) value);
    } else {
      ESP_LOGW(TAG, "parse failed");
    }
    ::close(sock_);
    sock_ = -1;
    waiting_ = false;
  } else {
    uint32_t elapsed = millis() - send_time_ms_;
    if (elapsed > 5000) {
      ESP_LOGW(TAG, "timeout after %u ms (errno=%d)", (unsigned) elapsed, errno);
      ::close(sock_);
      sock_ = -1;
      waiting_ = false;
    }
  }
}

void SNMPSensor::dump_config() {
  ESP_LOGCONFIG(TAG, "SNMP Sensor:");
  ESP_LOGCONFIG(TAG, "  Host: %s:%u", host_.c_str(), port_);
  ESP_LOGCONFIG(TAG, "  Community: %s", community_.c_str());
  ESP_LOGCONFIG(TAG, "  OID: %s", oid_.c_str());
  LOG_SENSOR("  ", "Sensor", this);
  LOG_UPDATE_INTERVAL(this);
}

}  // namespace snmp_sensor
}  // namespace esphome
