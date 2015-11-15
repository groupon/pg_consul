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

#include "boost/algorithm/string.hpp"
#include "boost/lexical_cast.hpp"
#include "consul/peers.hpp"


int
main(int argc, char* argv[]) {
  ::consul::Peer agent;

  switch (argc) {
    case 3: // ${PROG} ${HOSTNAME} ${PORT}
      if (!agent.setPort(argv[2])) {
        std::cerr << "Invalid consul port specification: " << argv[2] << std::endl;
        return EX_USAGE;
      }
      // fall through
    case 2: // ${PROG} ${HOSTNAME}
      if (!agent.setHost(argv[1])) {
        std::cerr << "Invalid consul host: " << argv[1] << std::endl;
        return EX_USAGE;
      }
      break;
    case 1: // ${PROG}
      break;
    default:
      std::cerr << "Too many arguments" << std::endl;
      return EX_USAGE;
  }

  try {
    auto r = cpr::Get(cpr::Url{consul::Peer::PeersUrl(agent)},
                      cpr::Header{{"Connection", "close"}},
                      cpr::Timeout{1000});
    if (r.status_code != 200) {
      std::cerr << "consul returned error " << r.status_code << std::endl;
      return EX_TEMPFAIL;
    }

    consul::Peers peers;
    std::string err;
    if (!::consul::Peers::InitFromJson(peers, r.text, err)) {
      std::cerr << "Failed to load peers from JSON: " << err << std::endl;
      return EX_PROTOCOL;
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
