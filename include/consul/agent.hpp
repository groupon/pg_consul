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
  using HostnameT = std::string;
  using PortT = std::uint16_t;
  using UrlT = std::string;
  const PortT DEFAULT_PORT = 8500;
  static constexpr const char* DEFAULT_HOST = "127.0.0.1";

  ClusterT cluster() const noexcept { return cluster_; }
  HostnameT host() const noexcept { return host_; }
  PortT port() const noexcept { return port_; }
  bool leader() const noexcept { return leader_; }

  Agent() : host_{DEFAULT_HOST}, port_{DEFAULT_PORT} {}
  Agent(HostnameT host) : host_{host}, port_{DEFAULT_PORT} {}
  Agent(PortT port) : host_{DEFAULT_HOST}, port_{port} {}
  Agent(HostnameT host, PortT port) : host_{host}, port_{port} {}

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

  std::string str() const {
    std::ostringstream ss;
    ss << host_ << ":" << port_;
    return ss.str();
  bool setCluster(const ClusterT cluster) noexcept {
    cluster_ = cluster;
    return true;
  }

  bool setHost(const HostnameT host) noexcept {
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

  bool operator==(const Agent& a) const noexcept {
    return (host_ == a.host_ && port_ == a.port_);
  }

private:
  void invalidateMemoizedUrls() noexcept {
    kvEndpointUrlPrefix_.clear();
    leaderUrl_.clear();
    peersUrl_.clear();
  }

  HostnameT host_ = DEFAULT_HOST;
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
