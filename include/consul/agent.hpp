#ifndef CONSUL_AGENT_HPP
#define CONSUL_AGENT_HPP

#include <cstdint>
#include <sstream>
#include <string>

#include "boost/algorithm/string.hpp"
#include "boost/lexical_cast.hpp"
#include "json11.hpp"

#include "consul/kv_pair.hpp"

namespace consul {
class Agent final {
public:
  using ClusterT = std::string;
  using PortT = std::uint16_t;
  using UrlT = std::string;
  const PortT DEFAULT_PORT = 8500;
  using HostT = std::string;
  static constexpr const char* DEFAULT_HOST = "127.0.0.1";

  using TimeoutT = std::uint16_t;
  static constexpr const TimeoutT DEFAULT_TIMEOUT_MS = 1000;
  static constexpr const TimeoutT DEFAULT_TIMEOUT_MS_MIN = std::numeric_limits<TimeoutT>::min() + 1;
  static constexpr const TimeoutT DEFAULT_TIMEOUT_MS_MAX = std::numeric_limits<TimeoutT>::max();

  ClusterT cluster() const noexcept { return cluster_; }
  HostT host() const noexcept { return host_; }
  PortT port() const noexcept { return port_; }
  TimeoutT timeoutMs() const noexcept { return timeout_ms_; }
  bool leader() const noexcept { return leader_; }

  Agent() : host_{DEFAULT_HOST}, port_{DEFAULT_PORT} {}
  Agent(HostT host) : host_{host}, port_{DEFAULT_PORT} {}
  Agent(PortT port) : host_{DEFAULT_HOST}, port_{port} {}
  Agent(HostT host, PortT port) : host_{host}, port_{port} {}

  UrlT kvUrl(const KVPair::KeyT& key) noexcept {
    std::string url;
    url.reserve(kvEndpointUrlPrefix().size() + key.size());
    url.assign(kvEndpointUrlPrefix());
    url.append(key);
    return url;
  }

  const UrlT& kvEndpointUrlPrefix() noexcept {
    if (kvEndpointUrlPrefix_.empty()) {
      std::ostringstream url;
      url << "http://" << host_ << ":" << port_ << "/v1/kv/";
      kvEndpointUrlPrefix_ = url.str();
    }
    return kvEndpointUrlPrefix_;
  }

  const UrlT& statusLeaderUrl() noexcept {
    if (leaderUrl_.empty()) {
      std::ostringstream url;
      url << "http://" << host_ << ":" << port_ << "/v1/status/leader";
      leaderUrl_ = url.str();
    }
    return leaderUrl_;
  }

  const UrlT& statusPeersUrl() noexcept {
    if (peersUrl_.empty()) {
      std::ostringstream url;
      url << "http://" << host_ << ":" << port_ << "/v1/status/peers";
      peersUrl_ = url.str();
    }
    return peersUrl_;
  }

  std::string json() const { return json11::Json(*this).dump(); };
  json11::Json to_json() const {
    std::ostringstream ss;
    ss << host_ << ":" << port_;
    return json11::Json{ ss.str() };
  }

  std::string portStr() const noexcept {
    std::string str;
    try {
      str = ::boost::lexical_cast<std::string>(port_);
    } catch(const ::boost::bad_lexical_cast &) {
      // Grr... shouldn't happen unless ENOMEM
    }
    return str;
  }

  bool setCluster(const ClusterT cluster) noexcept {
    cluster_ = cluster;
    return true;
  }

  bool setHost(const HostT host) noexcept {
    invalidateMemoizedUrls();
    host_ = host;
    return true;
  }

  bool setPort(const PortT port) noexcept {
    invalidateMemoizedUrls();
    port_ = port;
    return true;
  }

  bool setPort(const std::string& port) noexcept {
    try {
      invalidateMemoizedUrls();
      port_ = ::boost::lexical_cast<PortT>(port);
      return true;
    } catch(const ::boost::bad_lexical_cast &) {
      return false;
    }
  }

  std::string str() const {
    std::ostringstream ss;
    ss << host_ << ":" << port_;
    return ss.str();
  bool setTimeoutMs(const TimeoutT timeout_ms) noexcept {
    timeout_ms_ = timeout_ms;
    return true;
  }

  std::string timeoutMsStr() const {
    std::string str;
    try {
      str = ::boost::lexical_cast<std::string>(timeout_ms_);
    } catch (const std::exception& e) {
      // ENOMEM, fall through
    }
    return str;
  }

  std::string timeoutStr() const noexcept {
    std::string str;
    try {
      str = ::boost::lexical_cast<std::string>(timeout_ms_);
    } catch(const ::boost::bad_lexical_cast &) {
      // Grr... shouldn't happen unless ENOMEM
      str = std::string();
    }
    return str;
  }

  bool operator==(const Agent& a) const noexcept {
    return (host_ == a.host_ && port_ == a.port_);
  }

private:
  void invalidateMemoizedUrls() noexcept {
    kvEndpointUrlPrefix_.clear();
    leaderUrl_.clear();
    peersUrl_.clear();
  }

  TimeoutT timeout_ms_ = DEFAULT_TIMEOUT_MS;
  HostT host_ = DEFAULT_HOST;
  ClusterT cluster_;
  PortT port_ = DEFAULT_PORT;
  bool leader_ = false;

  // A collection of memoized URLs.  Assuming the backend will be long lived
  // and the URL won't change often.
  UrlT kvEndpointUrlPrefix_;
  UrlT leaderUrl_;
  UrlT peersUrl_;
};


} // namespace consul

#endif // CONSUL_AGENT_HPP
