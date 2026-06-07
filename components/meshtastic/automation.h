#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "esphome/core/automation.h"
#include "meshtastic.h"

namespace esphome {
namespace meshtastic {

// Fires for every decoded packet. (from, to, portnum, payload, rssi, snr)
class PacketTrigger : public Trigger<uint32_t, uint32_t, uint32_t, std::vector<uint8_t>, float, float> {
 public:
  explicit PacketTrigger(Meshtastic *parent) { parent->add_on_packet_trigger(this); }
};

// TEXT_MESSAGE_APP. (from, to, channel name, text, rssi, snr)
class TextTrigger : public Trigger<uint32_t, uint32_t, std::string, std::string, float, float> {
 public:
  explicit TextTrigger(Meshtastic *parent) { parent->add_on_text_trigger(this); }
};

// NODEINFO_APP. (from, channel, user, rssi, snr)
class NodeInfoTrigger : public Trigger<uint32_t, std::string, meshtastic_User, float, float> {
 public:
  explicit NodeInfoTrigger(Meshtastic *parent) { parent->add_on_nodeinfo_trigger(this); }
};

// POSITION_APP. (from, channel, position, rssi, snr)
class PositionTrigger : public Trigger<uint32_t, std::string, meshtastic_Position, float, float> {
 public:
  explicit PositionTrigger(Meshtastic *parent) { parent->add_on_position_trigger(this); }
};

// TELEMETRY_APP device-metrics. (from, channel, metrics, rssi, snr)
class TelemetryTrigger : public Trigger<uint32_t, std::string, meshtastic_DeviceMetrics, float, float> {
 public:
  explicit TelemetryTrigger(Meshtastic *parent) { parent->add_on_telemetry_trigger(this); }
};

// TELEMETRY_APP environment-metrics. (from, channel, metrics, rssi, snr)
class EnvironmentTrigger : public Trigger<uint32_t, std::string, meshtastic_EnvironmentMetrics, float, float> {
 public:
  explicit EnvironmentTrigger(Meshtastic *parent) { parent->add_on_environment_trigger(this); }
};

// TELEMETRY_APP air-quality-metrics. (from, channel, metrics, rssi, snr)
class AirQualityTrigger : public Trigger<uint32_t, std::string, meshtastic_AirQualityMetrics, float, float> {
 public:
  explicit AirQualityTrigger(Meshtastic *parent) { parent->add_on_air_quality_trigger(this); }
};

// TELEMETRY_APP power-metrics. (from, channel, metrics, rssi, snr)
class PowerTrigger : public Trigger<uint32_t, std::string, meshtastic_PowerMetrics, float, float> {
 public:
  explicit PowerTrigger(Meshtastic *parent) { parent->add_on_power_trigger(this); }
};

// TELEMETRY_APP local-stats. (from, channel, metrics, rssi, snr)
class LocalStatsTrigger : public Trigger<uint32_t, std::string, meshtastic_LocalStats, float, float> {
 public:
  explicit LocalStatsTrigger(Meshtastic *parent) { parent->add_on_local_stats_trigger(this); }
};

// TELEMETRY_APP health-metrics. (from, channel, metrics, rssi, snr)
class HealthTrigger : public Trigger<uint32_t, std::string, meshtastic_HealthMetrics, float, float> {
 public:
  explicit HealthTrigger(Meshtastic *parent) { parent->add_on_health_trigger(this); }
};

// NEIGHBORINFO_APP. (from, channel, info, rssi, snr)
class NeighborInfoTrigger : public Trigger<uint32_t, std::string, meshtastic_NeighborInfo, float, float> {
 public:
  explicit NeighborInfoTrigger(Meshtastic *parent) { parent->add_on_neighbor_info_trigger(this); }
};

// WAYPOINT_APP. (from, channel, waypoint, rssi, snr)
class WaypointTrigger : public Trigger<uint32_t, std::string, meshtastic_Waypoint, float, float> {
 public:
  explicit WaypointTrigger(Meshtastic *parent) { parent->add_on_waypoint_trigger(this); }
};

// Fires when the reply to a traceroute we initiated returns. (from, channel, route, rssi, snr): the decoded
// RouteDiscovery (route/snr_towards outbound, route_back/snr_back on the return; SNRs are dB*4 as int8).
class TraceRouteResponseTrigger : public Trigger<uint32_t, std::string, meshtastic_RouteDiscovery, float, float> {
 public:
  explicit TraceRouteResponseTrigger(Meshtastic *parent) { parent->add_on_traceroute_response_trigger(this); }
};

// meshtastic.send_text action. dest defaults to broadcast; channel is a channel name (empty = primary).
template<typename... Ts> class SendTextAction : public Action<Ts...> {
 public:
  explicit SendTextAction(Meshtastic *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(std::string, text)
  TEMPLATABLE_VALUE(uint32_t, dest)
  TEMPLATABLE_VALUE(std::string, channel)
  TEMPLATABLE_VALUE(bool, want_ack)

  void play(const Ts &...x) override {
    this->parent_->send_text(this->text_.value(x...), this->dest_.value(x...), this->channel_.value(x...),
                             this->want_ack_.value(x...));
  }

 protected:
  Meshtastic *parent_;
};

// meshtastic.send_position. precision_bits < 32 coarsens the location.
template<typename... Ts> class SendPositionAction : public Action<Ts...> {
 public:
  explicit SendPositionAction(Meshtastic *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(double, latitude)
  TEMPLATABLE_VALUE(double, longitude)
  TEMPLATABLE_VALUE(int32_t, altitude)
  TEMPLATABLE_VALUE(uint32_t, precision_bits)
  TEMPLATABLE_VALUE(std::string, channel)
  TEMPLATABLE_VALUE(bool, want_ack)

  void play(const Ts &...x) override {
    this->parent_->send_position(this->latitude_.value(x...), this->longitude_.value(x...),
                                 this->altitude_.value(x...), this->precision_bits_.value(x...),
                                 this->channel_.value(x...), this->want_ack_.value(x...));
  }

 protected:
  Meshtastic *parent_;
};

// meshtastic.send_telemetry: DeviceMetrics. Each field is sent only if provided (templatable from any sensor).
template<typename... Ts> class SendTelemetryAction : public Action<Ts...> {
 public:
  explicit SendTelemetryAction(Meshtastic *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(uint32_t, battery_level)
  TEMPLATABLE_VALUE(float, voltage)
  TEMPLATABLE_VALUE(float, channel_utilization)
  TEMPLATABLE_VALUE(float, air_util_tx)
  TEMPLATABLE_VALUE(uint32_t, uptime_seconds)
  TEMPLATABLE_VALUE(std::string, channel)
  TEMPLATABLE_VALUE(bool, want_ack)

  void play(const Ts &...x) override {
    meshtastic_DeviceMetrics m = meshtastic_DeviceMetrics_init_zero;
    if (this->battery_level_.has_value()) {
      m.has_battery_level = true;
      m.battery_level = this->battery_level_.value(x...);
    }
    if (this->voltage_.has_value()) {
      m.has_voltage = true;
      m.voltage = this->voltage_.value(x...);
    }
    if (this->channel_utilization_.has_value()) {
      m.has_channel_utilization = true;
      m.channel_utilization = this->channel_utilization_.value(x...);
    }
    if (this->air_util_tx_.has_value()) {
      m.has_air_util_tx = true;
      m.air_util_tx = this->air_util_tx_.value(x...);
    }
    if (this->uptime_seconds_.has_value()) {
      m.has_uptime_seconds = true;
      m.uptime_seconds = this->uptime_seconds_.value(x...);
    }
    this->parent_->send_device_metrics(m, this->channel_.value(x...), this->want_ack_.value(x...));
  }

 protected:
  Meshtastic *parent_;
};

// meshtastic.send_environment_metrics: EnvironmentMetrics. Each field is sent only if provided (templatable from any sensor).
template<typename... Ts> class SendEnvironmentMetricsAction : public Action<Ts...> {
 public:
  explicit SendEnvironmentMetricsAction(Meshtastic *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(float, temperature)
  TEMPLATABLE_VALUE(float, relative_humidity)
  TEMPLATABLE_VALUE(float, barometric_pressure)
  TEMPLATABLE_VALUE(float, gas_resistance)
  TEMPLATABLE_VALUE(float, voltage)
  TEMPLATABLE_VALUE(float, current)
  TEMPLATABLE_VALUE(float, distance)
  TEMPLATABLE_VALUE(float, lux)
  TEMPLATABLE_VALUE(float, white_lux)
  TEMPLATABLE_VALUE(float, ir_lux)
  TEMPLATABLE_VALUE(float, uv_lux)
  TEMPLATABLE_VALUE(float, wind_speed)
  TEMPLATABLE_VALUE(float, wind_gust)
  TEMPLATABLE_VALUE(float, wind_lull)
  TEMPLATABLE_VALUE(float, weight)
  TEMPLATABLE_VALUE(float, radiation)
  TEMPLATABLE_VALUE(float, rainfall_1h)
  TEMPLATABLE_VALUE(float, rainfall_24h)
  TEMPLATABLE_VALUE(float, soil_temperature)
  TEMPLATABLE_VALUE(uint32_t, iaq)
  TEMPLATABLE_VALUE(uint32_t, wind_direction)
  TEMPLATABLE_VALUE(uint32_t, soil_moisture)
  TEMPLATABLE_VALUE(std::vector<float>, one_wire_temperature)  // up to 8, e.g. multiple DS18B20
  TEMPLATABLE_VALUE(std::string, channel)
  TEMPLATABLE_VALUE(bool, want_ack)

  void play(const Ts &...x) override {
    meshtastic_EnvironmentMetrics m = meshtastic_EnvironmentMetrics_init_zero;
#define MESH_ENV_FLOAT(field) \
  if (this->field##_.has_value()) { \
    m.has_##field = true; \
    m.field = this->field##_.value(x...); \
  }
    MESH_ENV_FLOAT(temperature)
    MESH_ENV_FLOAT(relative_humidity)
    MESH_ENV_FLOAT(barometric_pressure)
    MESH_ENV_FLOAT(gas_resistance)
    MESH_ENV_FLOAT(voltage)
    MESH_ENV_FLOAT(current)
    MESH_ENV_FLOAT(distance)
    MESH_ENV_FLOAT(lux)
    MESH_ENV_FLOAT(white_lux)
    MESH_ENV_FLOAT(ir_lux)
    MESH_ENV_FLOAT(uv_lux)
    MESH_ENV_FLOAT(wind_speed)
    MESH_ENV_FLOAT(wind_gust)
    MESH_ENV_FLOAT(wind_lull)
    MESH_ENV_FLOAT(weight)
    MESH_ENV_FLOAT(radiation)
    MESH_ENV_FLOAT(rainfall_1h)
    MESH_ENV_FLOAT(rainfall_24h)
    MESH_ENV_FLOAT(soil_temperature)
#undef MESH_ENV_FLOAT
    if (this->iaq_.has_value()) {
      m.has_iaq = true;
      m.iaq = (uint16_t) this->iaq_.value(x...);
    }
    if (this->wind_direction_.has_value()) {
      m.has_wind_direction = true;
      m.wind_direction = (uint16_t) this->wind_direction_.value(x...);
    }
    if (this->soil_moisture_.has_value()) {
      m.has_soil_moisture = true;
      m.soil_moisture = (uint8_t) this->soil_moisture_.value(x...);
    }
    if (this->one_wire_temperature_.has_value()) {
      const std::vector<float> temps = this->one_wire_temperature_.value(x...);
      const size_t n = temps.size() < 8 ? temps.size() : 8;  // fixed array of 8 in the proto
      for (size_t i = 0; i < n; i++)
        m.one_wire_temperature[i] = temps[i];
      m.one_wire_temperature_count = n;
    }
    this->parent_->send_environment_metrics(m, this->channel_.value(x...), this->want_ack_.value(x...));
  }

 protected:
  Meshtastic *parent_;
};

// meshtastic.send_node_info: re-broadcast our own NodeInfo on demand
template<typename... Ts> class SendNodeInfoAction : public Action<Ts...> {
 public:
  explicit SendNodeInfoAction(Meshtastic *parent) : parent_(parent) {}
  void play(const Ts &...x) override { this->parent_->send_node_info(); }

 protected:
  Meshtastic *parent_;
};

// meshtastic.traceroute: probe the path to a node; the discovered route is logged when the reply returns.
template<typename... Ts> class SendTraceRouteAction : public Action<Ts...> {
 public:
  explicit SendTraceRouteAction(Meshtastic *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(uint32_t, dest)
  TEMPLATABLE_VALUE(std::string, channel)
  TEMPLATABLE_VALUE(bool, want_ack)

  void play(const Ts &...x) override {
    this->parent_->send_traceroute(this->dest_.value(x...), this->channel_.value(x...), this->want_ack_.value(x...));
  }

 protected:
  Meshtastic *parent_;
};

}  // namespace meshtastic
}  // namespace esphome
