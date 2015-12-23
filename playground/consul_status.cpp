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
#include <sstream>
#include <string>
#include <vector>

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
  ALL = std::numeric_limits<int>::max()
               ;
}; // namespace statusFlags

static int statusLeader(::consul::Agent& agent);
static int statusPeers(::consul::Agent& agent);

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

    std::vector<std::string> statusValues({"all", "leader", "peers"});
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

  return EX_OK;
}



static int
statusLeader(::consul::Agent& agent) {
  try {
    auto r = cpr::Get(cpr::Url{agent.statusLeaderUrl()},
                      cpr::Header{{"Connection", "close"}},
                      cpr::Timeout{1000});
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
                      cpr::Timeout{1000});
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
