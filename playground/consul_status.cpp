/*-------------------------------------------------------------------------
 *
 * consul_status.cpp	CLI interface for consul's status endpoints.
 *
 * Copyright (c) 2015, Groupon, Inc.
 *
 *-------------------------------------------------------------------------
 */

extern "C" {
#include <sysexits.h>
#include <unistd.h>
}

#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "boost/algorithm/string/join.hpp"

#include "cpr/cpr.h"
#define ELPP_NO_DEFAULT_LOG_FILE
#define ELPP_STACKTRACE_ON_CRASH
#define ELPP_STL_LOGGING
#define ELPP_THREAD_SAFE
#include "easylogging++.h"
#include "tclap/CmdLine.h"

#include "consul/agent.hpp"
#include "consul/peers.hpp"

INITIALIZE_EASYLOGGINGPP

static constexpr const char* COMMAND_HELP_MSG =
    u8"consul-status displays the current consul servers (peers) in the consul "
    u8"cluster according to the target consul agent.";

static bool debugFlag = false;

namespace StatusFlags {
// (ab)use namespaces to create an enumerated class-like entity that natively
// maps to a bitmask.
constexpr int
  NONE   = 0,
  LEADER = 1 << 1,
  PEERS  = 1 << 2,
  SELF   = 1 << 3,
  ALL = std::numeric_limits<int>::max();
} // namespace statusFlags

static int statusLeader(::consul::Agent& agent);
static int statusPeers(::consul::Agent& agent);
static int statusSelf(::consul::Agent& agent);

using ConsulPrefixSegmentT = std::string;
using ConsulPrefixT = std::vector<ConsulPrefixSegmentT>;
using ConsulKeyT = std::string;
using ConsulValueT = std::string;
using ConsulOidT = struct {
  ConsulValueT oidName;
  ConsulValueT description;
};
using ConsulOidLookupTupleT = std::pair<ConsulKeyT, ConsulOidT>;
using ConsulOidLookupMapT = std::map<ConsulOidLookupTupleT::first_type, ConsulOidLookupTupleT::second_type>;

static const ConsulOidLookupMapT configConsulConfigToOidMap = {
  { "ACLDatacenter", { "acl_datacenter", "" } },
  { "ACLDefaultPolicy", { "acl_default_policy", "" } },
  { "ACLDownPolicy", { "acl_down_policy", "" } },
  { "ACLTtl", { "acl_ttl", "" } },
  { "ACLTtlRaw", { "acl_ttl_raw", "" } },
  { "AdvertiseAddr",  { "advertise_addr", "" } },
  { "AdvertiseAddrWan", { "advertise_addr_wan", "" } },
  { "Atlas_endpoint", { "atlas_endpoint", "" } },
  { "Atlas_infrastructure", { "atlas_infrastructure", "" } },
  { "Atlas_join", { "atlas_join", "" } },
  { "BindAddr",       { "bind_addr", "" } },
  { "BootStrap",        { "bootstrap", "" } },
  { "BootstrapExpect",  { "bootstrap_expect", "" } },
  { "CAFile", { "ca_file", "" } },
  { "CertFile", { "cert_file", "" } },
  { "CheckUpdateInterval", { "check_update_interval", "" } },
  { "ClientAddr",     { "client_addr", "" } },
  { "DNSRecursor",      { "dns_recursor", ""} },
  { "DNSRecursorsefix", { "dns_recursors", ""} },
  { "DataDir",          { "data_dir", ""} },
  { "Datacenter",       { "datacenter", ""} },
  { "DisableAnonymousSignature", { "disable_anonymous_signature", "" } },
  { "DisableRemoteExec", { "disable_remote_exec", "" } },
  { "DisableUpdateCheck", { "disable_update_check", "" } },
  { "Disable_coordinates", { "disable_coordinates", "" } },
  { "DogStatsdAddr", { "dog_statsd_addr", "" } },
  { "DogStatsdTags", { "dog_statsd_tags", "" } },
  { "Domain",         { "domain", "" } },
  { "EnableDebug", { "enable_debug", "" } },
  { "EnableSyslog", { "enable_syslog", "" } },
  { "HTTPAPIResponseHeaders", { "http_api_response_headers", "" } },
  { "KeyFile", { "key_file", "" } },
  { "LeaveOnTerm",      { "leave_on_term", "" } },
  { "LogLevel",       { "log_level", "" } },
  { "NodeName",       { "node_name", "" } },
  { "PidFile", { "pid_file", "" } },
  { "Protocol", { "protocol", "" } },
  { "RejoinAfterLeave", { "rejoin_after_leave", "" } },
  { "RetryIntervalRaw", { "retry_interval_raw", "" } },
  { "RetryIntervalWanRaw", { "retry_interval_wan_raw", "" } },
  { "RetryJoin", { "retry_join", "" } },
  { "RetryJoinWan", { "retry_join_wan", "" } },
  { "RetryMaxAttempts", { "retry_max_attempts", "" } },
  { "RetryMaxAttemptsWan", { "retry_max_attempts_wan", "" } },
  { "Revision", { "revision", "" } },
  { "Server",           { "server", "" } },
  { "ServerName", { "server_name", "" } },
  { "SessionTTLMin", { "session_ttl_min", "" } },
  { "SessionTTLMinRaw", { "session_ttl_min_raw", "" } },
  { "SkipLeaveOnInt", { "skip_eave_on_int", "" } },
  { "StartJoin", { "start_join", "" } },
  { "StartJoinWan", { "start_join_wan", "" } },
  { "StatsdAddr", { "statsd_addr", "" } },
  { "StatsiteAddr", { "statsite_addr", "" } },
  { "StatsitePrefix", { "statsite_prefix", "" } },
  { "SyslogFacility", { "syslog_facility", "" } },
  { "UIDir", { "ui_dir", "" } },
  { "VerifyIncoming", { "verify_incoming", "" } },
  { "VerifyOutgoing", { "verify_outgoing", "" } },
  { "VerifyServerHostname", { "verify_server_hostname", "" } },
  { "Version", { "version", "" } },
  { "VersionPrerelease", { "version_prerelease", "" } },
  { "Watches", { "watches", "" } },
};

static const ConsulOidLookupMapT configConsulConfigDnsToOidMap = {
  { "AllowStale",     { "allow_stale", "Allow stale reads"} },
  { "EnableTruncate", { "enable_truncate", "Deliberately truncate DNS Responses to aid in randomization of responses"} },
  { "MaxStale",       { "max_stale", "Maximum drift permitted between follower and leader before follower stops returning DNS responses" } },
  { "NodeTTL",        { "node_ttl", "Node TTL"} },
  { "OnlyPassing",    { "only_passing", "Only return hosts with passing service and host checks" } },
  { "ServiceTTL",     { "service_ttl", "Service TTL"} },
};

static const ConsulOidLookupMapT configConsulConfigAdvertiseAddrsToOidMap = {
  { "RPC", { "rpc", "" } },
  { "RPCRaw", { "rpc_raw", "" } },
  { "SerfLan", { "serf_lan", "" } },
  { "SerfLanRaw", { "serf_lan_raw", "" } },
  { "SerfWan", { "serf_wan", "" } },
  { "SerfWanRaw", { "serf_wan_raw", "" } },
};

static const ConsulOidLookupMapT configConsulConfigPortsToOidMap = {
  { "DNS", { "dns", "" } },
  { "HTTP", { "http", "" } },
  { "HTTPS", { "https", "" } },
  { "RPC", { "rpc", "" } },
  { "SerfLan", { "serf_lan", "" } },
  { "SerfWan", { "serf_wan", "" } },
  { "Server", { "server", "" } },
};

static const ConsulOidLookupMapT configConsulConfigAddressesToOidMap = {
  { "DNS", { "dns", "" } },
  { "HTTP", { "http", "" } },
  { "HTTPS", { "https", "" } },
  { "RPC", { "rpc", "" } },
};

static const ConsulOidLookupMapT configConsulConfigUnixSocketsToOidMap = {
  { "Group", { "grp", "" } },
  { "Perms", { "perms", "" } },
  { "User", { "usr", "" } },
};

static const ConsulOidLookupMapT configConsulCoordToOidMap = {
  { "Adjustment", { "adjustment", "" } },
  { "Error", { "error", "" } },
  { "Height", { "height", "" } },
  { "Vec", { "vec", "" } },
};

static const ConsulOidLookupMapT configConsulMemberToOidMap = {
  { "Addr", { "addr", "" } },
  { "DelegateCur", { "delegate_cur", "" } },
  { "DelegateMax", { "delegate_max", "" } },
  { "DelegateMin", { "delegate_min", "" } },
  { "Name", { "name", "" } },
  { "Port", { "port", "" } },
  { "ProtocolCur", { "protocol_cur", "" } },
  { "ProtocolMax", { "protocol_max", "" } },
  { "ProtocolMin", { "protocol_min", "" } },
  { "Status", { "status", "" } },
};

static const ConsulOidLookupMapT configConsulMemberTagsToOidMap = {
  { "bootstrap", { "bootstrap", "" } },
  { "build", { "build", "" } },
  { "dc", { "dc", "" } },
  { "port", { "port", "" } },
  { "role", { "role", "" } },
  { "vsn", { "vsn", "" } },
  { "vsn_max", { "vsn_max", "" } },
  { "vsn_min", { "vsn_min", "" } },
};



const std::string
formatConsulOidFullValue(const ConsulPrefixT& prefix, const ConsulKeyT& consulKey, const ConsulValueT& consulVal) {
  std::ostringstream ss;
  ss << boost::algorithm::join(prefix, ".") << "." << consulKey << "=" << consulVal;
  return ss.str();
}


const ConsulPrefixT
appendPrefix(const ConsulPrefixT& prefix, const ConsulPrefixSegmentT& additionalSegment) {
  ConsulPrefixT newPrefix(prefix.size() + 1);
  newPrefix.assign(prefix.begin(), prefix.end());
  newPrefix.emplace_back(additionalSegment);
  return newPrefix;
}


int
main(int argc, char* argv[]) {
  el::Configurations defaultConf;
  defaultConf.setToDefault();
  defaultConf.setGlobally(el::ConfigurationType::ToFile, std::string("false"));
  defaultConf.setGlobally(el::ConfigurationType::ToStandardOutput, std::string("true"));
  el::Loggers::reconfigureLogger("default", defaultConf);
  if (::isatty(::fileno(stdout)))
    el::Loggers::addFlag(el::LoggingFlag::ColoredTerminalOutput);

  ::consul::Agent agent;
  auto statusFlags = StatusFlags::NONE;

  try {
    TCLAP::CmdLine cmd(COMMAND_HELP_MSG, '=', "0.1");

    // Args are displayed LIFO
    TCLAP::SwitchArg debugArg("d", "debug", "Print additional information with debugging", false);
    cmd.add(debugArg);

    TCLAP::ValueArg<consul::Agent::PortT> portArg("p", "port", "Port number of consul agent", false, agent.port(), "port");
    cmd.add(portArg);

    TCLAP::ValueArg<consul::Agent::HostT> hostArg("H", "host", "Hostname of consul agent", false, agent.host().c_str(), "hostname");
    cmd.add(hostArg);

    std::vector<std::string> statusValues({"all", "leader", "peers", "self"});
    TCLAP::ValuesConstraint<std::string> statusConstraint(statusValues);
    TCLAP::ValueArg<std::string> statusTypeArg("s", "status", "Type of status to fetch", false, "all", &statusConstraint);
    cmd.add(statusTypeArg);

    cmd.parse(argc, argv);

    if (debugArg.isSet()) {
      debugFlag = debugArg.getValue();
    }

    if (hostArg.isSet()) {
      agent.setHost(hostArg.getValue());
    }

    if (portArg.isSet()) {
      agent.setPort(portArg.getValue());
    }

    if (statusTypeArg.isSet()) {
      const auto& v = statusTypeArg.getValue();
      if (v == "all") {
        statusFlags |= StatusFlags::ALL;
      } else if (v == "leader") {
        statusFlags |= StatusFlags::LEADER;
      } else if (v == "peers") {
        statusFlags |= StatusFlags::PEERS;
      } else if (v == "self") {
        statusFlags |= StatusFlags::SELF;
      } else {
        LOG(FATAL) << "Unsupported status type: " << v;
      }
    } else {
      statusFlags |= StatusFlags::ALL;
    }
  } catch (TCLAP::ArgException &e)  {
    LOG(FATAL) << e.error() << " for arg " << e.argId();
    return EX_USAGE;
  }

  if (statusFlags == StatusFlags::NONE) {
    LOG(FATAL) << "No status type specified";
  }

  if (statusFlags & StatusFlags::LEADER) {
    auto ret = statusLeader(agent);
    if (ret != EX_OK)
      return ret;
  }

  if (statusFlags & StatusFlags::PEERS) {
    auto ret = statusPeers(agent);
    if (ret != EX_OK)
      return ret;
  }

  if (statusFlags & StatusFlags::SELF) {
    auto ret = statusSelf(agent);
    if (ret != EX_OK)
      return ret;
  }

  return EX_OK;
}



static int
statusLeader(::consul::Agent& agent) {
  try {
    auto r = cpr::Get(cpr::Url{agent.statusLeaderUrl()},
                      cpr::Header{{"Connection", "close"}},
                      cpr::Timeout{agent.timeoutMs()});
    if (r.status_code != 200) {
      LOG(ERROR) << "consul returned error " << r.status_code;
      return EX_TEMPFAIL;
    }

    ::consul::Peer leader;
    {
      std::string err;
      if (!::consul::Peer::InitFromJson(leader, r.text, err)) {
        LOG(ERROR) << "Failed to load leader from JSON: " << err;
        return EX_PROTOCOL;
      }

      // This is racy, but since we fetched from the statusLeaderUrl(), we
      // can make an assumption about the status of this particular Peer.
      leader.leader = true;
    }

    std::cout << "Leader: " << leader.str() << std::endl;
    LOG_IF(debugFlag, INFO) << "Host: " << leader.host;
    LOG_IF(debugFlag, INFO) << "Port: " << leader.port;
    LOG_IF(debugFlag, INFO) << "JSON: " << leader.json();
    return EX_OK;
  } catch (std::exception & e) {
    LOG(FATAL) << "cpr threw an exception: " << e.what();
    return EX_SOFTWARE;
  }
}


static int
statusPeers(::consul::Agent& agent) {
  try {
    auto r = cpr::Get(cpr::Url{agent.statusPeersUrl()},
                      cpr::Header{{"Connection", "close"}},
                      cpr::Timeout{agent.timeoutMs()});
    if (r.status_code != 200) {
      LOG(ERROR) << "consul returned error " << r.status_code;
      return EX_TEMPFAIL;
    }

    ::consul::Peers peers;
    {
      std::string err;
      if (!::consul::Peers::InitFromJson(peers, r.text, err)) {
        LOG(ERROR) << "Failed to load peers from JSON: " << err;
        return EX_PROTOCOL;
      }
    }

    for (auto& peer : peers.peers) {
      std::cout << "JSON Peer: " << json11::Json(peer).dump() << std::endl;
    }

    return EX_OK;
  } catch (std::exception & e) {
    LOG(FATAL) << "cpr threw an exception: " << e.what();
    return EX_SOFTWARE;
  }
}


static void
statusSelfConfigAdvertiseAddrs(const json11::Json& cfg, const ConsulPrefixT& basePrefix) {
  auto prefix = appendPrefix(basePrefix, "advertise_addrs");
  for (const auto& kv : configConsulConfigAdvertiseAddrsToOidMap) {
    std::cout << formatConsulOidFullValue(prefix, kv.second.oidName, cfg[kv.first].dump()) << std::endl;
  }
}


static void
statusSelfConfigDns(const json11::Json& cfg, const ConsulPrefixT& basePrefix) {
  auto prefix = appendPrefix(basePrefix, "dns_config");
  for (const auto& kv : configConsulConfigDnsToOidMap) {
    std::cout << formatConsulOidFullValue(prefix, kv.second.oidName, cfg[kv.first].dump()) << std::endl;
  }
}


static void
statusSelfConfigPorts(const json11::Json& cfg, const ConsulPrefixT& basePrefix) {
  auto prefix = appendPrefix(basePrefix, "ports");
  for (const auto& kv : configConsulConfigPortsToOidMap) {
    std::cout << formatConsulOidFullValue(prefix, kv.second.oidName, cfg[kv.first].dump()) << std::endl;
  }
}


static void
statusSelfConfigAddresses(const json11::Json& cfg, const ConsulPrefixT& basePrefix) {
  auto prefix = appendPrefix(basePrefix, "addresses");
  for (const auto& kv : configConsulConfigAddressesToOidMap) {
    std::cout << formatConsulOidFullValue(prefix, kv.second.oidName, cfg[kv.first].dump()) << std::endl;
  }
}

static void
statusSelfConfigUnixSockets(const json11::Json& cfg, const ConsulPrefixT& basePrefix) {
  auto prefix = appendPrefix(basePrefix, "unix_sockets");
  for (const auto& kv : configConsulConfigUnixSocketsToOidMap) {
    std::cout << formatConsulOidFullValue(prefix, kv.second.oidName, cfg[kv.first].dump()) << std::endl;
  }
}


static void
statusSelfConfig(const json11::Json& cfg, const ConsulPrefixT& basePrefix) {
  auto prefix = appendPrefix(basePrefix, "config");

  // FIXME(seanc@): Sort everything by the oidName, not its Consul Name
  //std::sort(configConsulConfigToOidMap.begin(), configConsulConfigToOidMaps.end(),
  //  [](ConsulOidLookupTupleT const & a, ConsulOidLookupTupleT const & b) { return a.second.oidName < b.second.oidName });

  // Iterate over sorted oid map
  for (const auto& kv : configConsulConfigToOidMap) {
    std::cout << formatConsulOidFullValue(prefix, kv.second.oidName, cfg[kv.first].dump()) << std::endl;
  }

  statusSelfConfigDns(cfg["DNSConfig"], prefix);
  statusSelfConfigAdvertiseAddrs(cfg["AdvertiseAddrs"], prefix);
  statusSelfConfigPorts(cfg["Ports"], prefix);
  statusSelfConfigAddresses(cfg["Addresses"], prefix);
  statusSelfConfigUnixSockets(cfg["UnixSockets"], prefix);
}


static void
statusSelfCoord(const json11::Json& cfg, const ConsulPrefixT& basePrefix) {
  auto prefix = appendPrefix(basePrefix, "coord");

  for (const auto& kv : configConsulCoordToOidMap) {
    std::cout << formatConsulOidFullValue(prefix, kv.second.oidName, cfg[kv.first].dump()) << std::endl;
  }
}



static void
statusSelfMemberTags(const json11::Json& cfg, const ConsulPrefixT& basePrefix) {
  auto prefix = appendPrefix(basePrefix, "tags");

  for (const auto& kv : configConsulMemberTagsToOidMap) {
    std::cout << formatConsulOidFullValue(prefix, kv.second.oidName, cfg[kv.first].dump()) << std::endl;
  }
}


static void
statusSelfMember(const json11::Json& cfg, const ConsulPrefixT& basePrefix) {
  auto prefix = appendPrefix(basePrefix, "member");

  for (const auto& kv : configConsulMemberToOidMap) {
    std::cout << formatConsulOidFullValue(prefix, kv.second.oidName, cfg[kv.first].dump()) << std::endl;
  }

  statusSelfMemberTags(cfg["Tags"], prefix);
}


static int
statusSelf(::consul::Agent& agent) {
  try {
    auto r = cpr::Get(cpr::Url{agent.selfUrl()},
                      cpr::Header{{"Connection", "close"}},
                      cpr::Timeout{agent.timeoutMs()});
    if (r.status_code != 200) {
      LOG(ERROR) << "consul returned error " << r.status_code;
      return EX_TEMPFAIL;
    }

    // Parse response as json
    std::string err;
    auto jsr = json11::Json::parse(r.text, err);
    if (!err.empty()) {
      LOG(ERROR) << "Unable to parse json: " << err;
      return EX_PROTOCOL;
    }

    const ConsulPrefixT prefix = {"consul", "agent", "status"};
    statusSelfConfig(jsr["Config"], prefix);
    statusSelfCoord(jsr["Coord"], prefix);
    statusSelfMember(jsr["Member"], prefix);

    return EX_OK;
  } catch (std::exception & e) {
    LOG(FATAL) << "cpr threw an exception: " << e.what();
    return EX_SOFTWARE;
  }
}
