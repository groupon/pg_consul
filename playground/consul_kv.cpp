/*-------------------------------------------------------------------------
 *
 * consul_kv.cpp	CLI interface for consul's key/value store
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

#include "b64/decode.hpp"
#include "b64/encode.hpp"
#include "cpr/cpr.h"
#define ELPP_NO_DEFAULT_LOG_FILE
#define ELPP_STACKTRACE_ON_CRASH
#define ELPP_STL_LOGGING
#define ELPP_THREAD_SAFE
#include "easylogging++.h"
#include "tclap/CmdLine.h"

#include "consul/agent.hpp"
#include "consul/kv_pairs.hpp"

INITIALIZE_EASYLOGGINGPP

static constexpr const char* COMMAND_HELP_MSG =
    u8R"msg(consul_kv displays all details of a stored key in the consul cluster
according to the target consul agent.)msg";
static bool debugFlag = false;


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
        clusterArg("c", "cluster", "consul Cluster (i.e. '?dc=<cluster>')", false, "", "dc1");
    cmd.add(clusterArg);

    TCLAP::SwitchArg debugArg("d", "debug", "Print additional information with debugging", false);
    cmd.add(debugArg);

    TCLAP::SwitchArg recursiveArg("r", "recursive", "Pass the '?recurse' query parameter to recursively find all keys");
    cmd.add(recursiveArg);

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

    if (recursiveArg.isSet()) {
      agent.setRecursive(recursiveArg.getValue());
    }
  } catch (TCLAP::ArgException &e)  {
    LOG(FATAL) << e.error() << " for arg " << e.argId();
    return EX_USAGE;
  }

  try {
    for (auto& key : keys) {
      auto kvUrl = agent.kvUrl(key);
      LOG_IF(debugFlag, INFO) << "Key URL: " << kvUrl;

      auto params = cpr::Parameters();
      if (!agent.cluster().empty()) {
        LOG_IF(debugFlag, INFO) << "Passing ?dc=" << agent.cluster() << " flag";
        params.AddParameter({"dc", agent.cluster()});
      } else {
        LOG_IF(debugFlag, INFO) << "No consul ?dc= specified";
      }

      if (agent.recursive()) {
        LOG_IF(debugFlag, INFO) << "Passing ?recurse flag";
        params.AddParameter({"recurse", ""});
      } else {
        LOG_IF(debugFlag, INFO) << "Not passing the ?recurse flag";
      }

      auto r = cpr::Get(cpr::Url{kvUrl},
                        cpr::Header{{"Connection", "close"}},
                        cpr::Timeout{1000},
                        params);
      if (r.status_code != 200) {
        LOG(ERROR) << "consul agent returned error " << r.status_code;
        return EX_TEMPFAIL;
      }
      DLOG_IF(debugFlag, INFO) << "URL: " << r.url;

      consul::KVPairs kvps;
      std::string err;
      if (!::consul::KVPairs::InitFromJson(kvps, r.text, err)) {
        LOG(ERROR) << "Failed to load KVPair(s) from JSON: " << err;
        return EX_PROTOCOL;
      }

      if (agent.recursive()) {
        LOG_IF(debugFlag, INFO) << "Found " << kvps.size() << " objects when searching for " << kvUrl;
      } else {
        if (kvps.size() > 1) {
          LOG(ERROR) << "Non-recursive GET for " << r.url << " returned " << kvps.size() << " objects";
          return EX_PROTOCOL;
        }
      }

      std::size_t i = 0;
      for (auto& kvp : kvps.objs()) {
        if (agent.recursive()) {
          std::cout << "========== BEGIN KEY " << ++i << "/" << kvps.size() << " ==========" << std::endl;
        }
        std::cout << "Key: " << kvp.key() << std::endl;
        std::cout << "Value: " << kvp.value() << std::endl;
        if (!kvp.session().empty()) {
          std::cout << "Session: " << kvp.session() << std::endl;
        }

        DLOG_IF(debugFlag, INFO) << "Value (base64-encoded): " << kvp.valueEncoded();
        LOG_IF(debugFlag, INFO) << "CreateIndex: " << kvp.createIndex();
        LOG_IF(debugFlag, INFO) << "ModifyIndex: " << kvp.modifyIndex();
        LOG_IF(debugFlag, INFO) << "LockIndex: "   << kvp.lockIndex();
        LOG_IF(debugFlag, INFO) << "Flags: "       << kvp.flags();
        LOG_IF(debugFlag, INFO) << "Session: "     << (!kvp.session().empty() ? kvp.session() : "[none]");
        LOG_IF(debugFlag, INFO) << "JSON: " << kvp.json();
      }
    }

    return EX_OK;
  } catch (std::exception & e) {
    LOG(FATAL) << "cpr threw an exception: " << e.what();
    return EX_SOFTWARE;
  }

  return EX_OK;
}
