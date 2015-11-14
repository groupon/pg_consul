/*-------------------------------------------------------------------------
 *
 * consul_leader.cpp	PostgreSQL interface for consul
 *
 * Copyright (c) 2015, Sean Chittenden
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
#include "json11.hpp"
#include "consul.hpp"


int
main(int argc, char* argv[]) {
  using json11::Json;
  using consul::Peer;

  Peer leader;

  switch (argc) {
    case 3: // ${PROG} ${HOSTNAME} ${PORT}
      if (!leader.setPort(argv[2])) {
        std::cerr << "Invalid consul port specification: " << argv[2] << std::endl;
        return EX_USAGE;
      }
      // fall through
    case 2: // ${PROG} ${HOSTNAME}
      if (!leader.setHost(argv[1])) {
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
    auto r = cpr::Get(cpr::Url{::consul::Peer::LeaderUrl(leader)},
                      cpr::Header{{"Connection", "close"}},
                      cpr::Timeout{1000});
    if (r.status_code != 200) {
      std::cerr << "consul returned error " << r.status_code << std::endl;
      return EX_TEMPFAIL;
    }

    std::string err;
    if (!::consul::Peer::InitFromJson(leader, r.text, err)) {
      std::cerr << "Failed to load leader from JSON: " << err << std::endl;
      return EX_PROTOCOL;
    }
  } catch (std::exception & e) {
    std::cerr << "cpr threw an exception: " << e.what() << std::endl;
    return EX_SOFTWARE;
  }

  std::cout << "Host: " << leader.host << std::endl;
  std::cout << "Port: " << leader.port << std::endl;
  std::cout << "Peer: " << leader.str() << std::endl;
  std::cout << "JSON1: " << json11::Json(leader).dump() << std::endl;
  std::cout << "JSON2: " << leader.json() << std::endl;
  return EX_OK;
}
