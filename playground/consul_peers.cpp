/*-------------------------------------------------------------------------
 *
 * consul_peers.cpp	CLI interface for consul's status peers endpoint.
 *
 * Copyright (c) 2015, Groupon, Inc.
 *
 *-------------------------------------------------------------------------
 */

extern "C" {
#include "sysexits.h"
}

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "cpr/cpr.h"
#include "tclap/CmdLine.h"

#include "consul/agent.hpp"
#include "consul/peers.hpp"

static constexpr const char* COMMAND_HELP_MSG =
    u8R"msg(consul_peers displays the current consul servers (peers) in the consul cluster according to the target consul agent.)msg";
static bool debugFlag = false;

int
main(int argc, char* argv[]) {
  ::consul::Agent agent;

  try {
    TCLAP::CmdLine cmd(COMMAND_HELP_MSG, '=', "0.1");

    // Args are displayed LIFO
    TCLAP::SwitchArg debugArg("d", "debug", "Print additional information with debugging", false);
    cmd.add(debugArg);

    TCLAP::ValueArg<consul::Agent::PortT> portArg("p", "port", "Port number of consul agent", false, agent.port(), "port");
    cmd.add(portArg);

    TCLAP::ValueArg<consul::Agent::HostnameT> hostArg("H", "host", "Hostname of consul agent", false, agent.host().c_str(), "hostname");
    cmd.add(hostArg);

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
  } catch (TCLAP::ArgException &e)  {
    std::cerr << "ERROR: " << e.error() << " for arg " << e.argId() << std::endl;
    return EX_USAGE;
  }

  try {
    auto r = cpr::Get(cpr::Url{agent.statusPeersUrl()},
                      cpr::Header{{"Connection", "close"}},
                      cpr::Timeout{1000});
    if (r.status_code != 200) {
      std::cerr << "consul returned error " << r.status_code << std::endl;
      return EX_TEMPFAIL;
    }

    ::consul::Peers peers;
    {
      std::string err;
      if (!::consul::Peers::InitFromJson(peers, r.text, err)) {
        std::cerr << "Failed to load peers from JSON: " << err << std::endl;
        return EX_PROTOCOL;
      }
    }

    for (auto& peer : peers.peers) {
      std::cout << "JSON Peer: " << json11::Json(peer).dump() << std::endl;
    }

    return EX_OK;
  } catch (std::exception & e) {
    std::cerr << "cpr threw an exception: " << e.what() << std::endl;
    return EX_SOFTWARE;
  }

  return EX_OK;
}
