/*-------------------------------------------------------------------------
 *
 * consul_kv.cpp	CLI interface for consul's key/value store
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
#include "b64/decode.hpp"
#include "b64/encode.hpp"

#include "consul/agent.hpp"
#include "consul/kv_pair.hpp"
#include "consul/peers.hpp"

static constexpr const char* COMMAND_HELP_MSG =
    u8R"msg(consul_kv displays all details of a stored key in the consul cluster
according to the target consul agent.)msg";
static bool debugFlag = false;


int
main(int argc, char* argv[]) {
  ::consul::Agent agent;
  std::vector<::consul::KVPair::KeyT> keys;

  try {
    TCLAP::CmdLine cmd(COMMAND_HELP_MSG, '=', "0.1");

    // Args are displayed LIFO
    std::vector<TCLAP::Arg*> cmdMajorModes;
    TCLAP::MultiArg<::consul::KVPair::KeyT>
        keyArg("k", "key", "Key to fetch from the consul agent", true, "string");
    cmdMajorModes.push_back(&keyArg);

    TCLAP::ValueArg<std::string>
        decodeArg("D", "decode", "Decode a base64 encoded string", false,
                  "dGVzdA==", "base64-encoded-string");
    cmdMajorModes.push_back(&decodeArg);

    TCLAP::ValueArg<std::string>
        encodeArg("E", "encode", "Encode a stream of bytes using base64", false,
                  "test", "stream of bytes");
    cmdMajorModes.push_back(&encodeArg);
    cmd.xorAdd(cmdMajorModes);

    TCLAP::ValueArg<std::string>
        clusterArg("c", "cluster", "consul Cluster (i.e. data center)", false, "", "dc1");
    cmd.add(clusterArg);

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

    if (decodeArg.isSet()) {
      base64::decoder D;
      const std::string testInput{decodeArg.getValue()};
      std::istringstream iss{testInput};
      std::ostringstream oss;
      D.decode(iss, oss);
      std::cout << "Result: \"" << oss.str() << "\"" << std::endl;
      return EX_OK;
    }

    if (encodeArg.isSet()) {
      base64::encoder E;
      const std::string testInput{encodeArg.getValue()};
      std::istringstream iss{testInput};
      std::ostringstream oss;
      E.encode(iss, oss);
      std::cout << "-----BEGIN BASE64 ENCODED STREAM-----" << std::endl
                << oss.str()
                << "-----END BASE64 ENCODED STREAM-----" << std::endl
          ;
      return EX_OK;
    }

    keys = keyArg.getValue();
    if (clusterArg.isSet()) {
      agent.setCluster(clusterArg.getValue());
    }
  } catch (TCLAP::ArgException &e)  {
    std::cerr << "ERROR: " << e.error() << " for arg " << e.argId() << std::endl;
    return EX_USAGE;
  }

  try {
    for (auto& key : keys) {
      auto kvUrl = agent.kvUrl(key);
      if (debugFlag) {
        std::cerr << "Key URL: " << kvUrl << std::endl;
      }

      auto params = cpr::Parameters();
      if (!agent.cluster().empty()) {
        params.AddParameter({"dc", agent.cluster()});
      }

      auto r = cpr::Get(cpr::Url{kvUrl},
                        cpr::Header{{"Connection", "close"}},
                        cpr::Timeout{1000},
                        params);
      if (r.status_code != 200) {
        std::cerr << "consul agent returned error " << r.status_code << std::endl;
        return EX_TEMPFAIL;
      }

      consul::KVPair kvp;
      std::string err;
      if (!::consul::KVPair::InitFromJson(kvp, r.text, err)) {
        std::cerr << "Failed to load KVPair from JSON: " << err << std::endl;
        return EX_PROTOCOL;
      }

      std::cout << "Key: " << kvp.key() << std::endl;
      std::cout << "Value: " << kvp.value() << std::endl;
      if (!kvp.session().empty()) {
        std::cout << "Session: " << kvp.session() << std::endl;
      }

      if (debugFlag) {
        std::cout
            << "CreateIndex: " << kvp.createIndex() << std::endl
            << "ModifyIndex: " << kvp.modifyIndex() << std::endl
            << "LockIndex: "   << kvp.lockIndex() << std::endl
            << "Flags: "       << kvp.flags() << std::endl
            << "Session: "     << (!kvp.session().empty() ? kvp.session() : "[none]") << std::endl
            << "Value (base64-encoded): " << std::endl
            << consul::KVPair::Base64Header << std::endl
            << kvp.valueEncoded() << std::endl
            << consul::KVPair::Base64Footer << std::endl
            << "JSON: " << kvp.json() << std::endl;
      }
    }

    return EX_OK;
  } catch (std::exception & e) {
    std::cerr << "cpr threw an exception: " << e.what() << std::endl;
    return EX_SOFTWARE;
  }

  return EX_OK;
}
