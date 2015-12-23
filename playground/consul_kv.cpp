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
#include <limits>
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
    u8"consul-kv displays all details of a stored key in the consul cluster "
    u8"according to the target consul agent.";


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
  ::consul::KVPair::ValueT value;
  TCLAP::ValueArg<::consul::KVPair::IndexT>
      casArg("C", "cas", "Check-and-Set index. When performing a PUT or DELETE, only operate if the ModifyIndex matches the passed in CAS value",
             false, std::numeric_limits<::consul::KVPair::IndexT>::max(), "modify-index");
  TCLAP::ValueArg<::consul::KVPair::SessionT>
      sessionArg("S", "session", "Acquire a lock using the specified Session",
                 false, "", "session");
  TCLAP::ValueArg<::consul::KVPair::FlagsT>
      flagsArg("F", "flags", "Opaque numeric value attached to a key (0 through (2^64)-1)",
               false, std::numeric_limits<::consul::KVPair::FlagsT>::max() - 1, "flag");
  enum class MethodType : char { GET, PUT, DELETE };
  MethodType methodType;
  bool debugFlag = false;
  bool recursiveFlag = false;

  std::vector<::consul::KVPair::KeyT> keys;

  try {
    TCLAP::CmdLine cmd(COMMAND_HELP_MSG, '=', "0.1");

    // Args are displayed LIFO
    std::vector<TCLAP::Arg*> cmdMajorModes;
    TCLAP::MultiArg<::consul::KVPair::KeyT>
        keyArg("k", "key", "Key(s) operate on with the consul agent", true, "string");
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

    TCLAP::SwitchArg debugArg("d", "debug", "Print additional information with debugging", false);
    cmd.add(debugArg);

    TCLAP::ValueArg<consul::Agent::PortT> portArg("p", "port", "Port number of consul agent", false, agent.port(), "port");
    cmd.add(portArg);

    TCLAP::ValueArg<consul::Agent::HostT> hostArg("H", "host", "Hostname of consul agent", false, agent.host().c_str(), "hostname");
    cmd.add(hostArg);

    std::vector<std::string> methods{{"GET","PUT","DELETE"}};
    TCLAP::ValuesConstraint<std::string> methodConstraint(methods);
    TCLAP::ValueArg<std::string> methodArg("m", "method", "HTTP method used to act on the key", false, "get", &methodConstraint);
    cmd.add(methodArg);

    TCLAP::ValueArg<std::string>
        valueArg("v", "value", "Value to be used when PUT'ing a key (will be base64 encoded automatically).", false, "", "value");
    cmd.add(valueArg);

    TCLAP::SwitchArg recursiveArg("r", "recursive", "Pass the '?recurse' query parameter to recursively find all keys");
    cmd.add(recursiveArg);

    // NOTE: the following *Args were defined above try{} block
    cmd.add(casArg);
    cmd.add(flagsArg);
    cmd.add(sessionArg);

    TCLAP::ValueArg<std::string>
        clusterArg("c", "cluster", "consul Cluster (i.e. '?dc=<cluster>')", false, "", "dc1");
    cmd.add(clusterArg);

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
      recursiveFlag = recursiveArg.getValue();
    }

    if (methodArg.isSet()) {
      const auto& v = methodArg.getValue();
      if (v == "GET") {
        methodType = MethodType::GET;
      } else if (v == "PUT") {
        if (!valueArg.isSet()) {
          LOG(ERROR) << "Unable to use PUT method without specifying a value argument (--value=)";
          return EX_USAGE;
        }
        if (keys.size() > 1) {
          LOG(ERROR) << "Unable to pass multiple key arguments when PUT'ing a value";
          return EX_USAGE;
        }
        methodType = MethodType::PUT;
      } else if (v == "DELETE") {
        methodType = MethodType::DELETE;
      } else {
        LOG(FATAL) << "Unknown type: " << v;
        return EX_SOFTWARE;
      }
    }

    if (valueArg.isSet()) {
      value = valueArg.getValue();
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

      if (recursiveFlag && (methodType == MethodType::GET ||
                            methodType == MethodType::DELETE)) {
        LOG_IF(debugFlag, INFO) << "Passing ?recurse flag";
        params.AddParameter({"recurse", ""});
      } else {
        LOG_IF(debugFlag, INFO) << "Not passing the ?recurse flag";
      }

      if (casArg.isSet() && (methodType == MethodType::PUT ||
                             methodType == MethodType::DELETE)) {
        auto casStr = ::boost::lexical_cast<std::string>(casArg.getValue());
        LOG_IF(debugFlag, INFO) << "Passing ?cas=" << casStr << " parameter";
        params.AddParameter({"cas", casStr});
      }

      if (sessionArg.isSet() && methodType == MethodType::PUT) {
        LOG_IF(debugFlag, INFO) << "Passing ?acquire=" << sessionArg.getValue() << " parameter";
        params.AddParameter({"acquire", sessionArg.getValue()});
      }

      if (flagsArg.isSet() && methodType == MethodType::PUT) {
        auto flagsStr = ::boost::lexical_cast<std::string>(flagsArg.getValue());
        LOG_IF(debugFlag, INFO) << "Passing ?flags=" << flagsStr << " parameter";
        params.AddParameter({"flags", flagsStr});
      }

      if (methodType == MethodType::GET) {
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

        if (recursiveFlag) {
          LOG_IF(debugFlag, INFO) << "Found " << kvps.size() << " objects when searching for " << kvUrl;
        } else {
          if (kvps.size() > 1) {
            LOG(ERROR) << "Non-recursive GET for " << r.url << " returned " << kvps.size() << " objects";
            return EX_PROTOCOL;
          }
        }

        std::size_t i = 0;
        for (auto& kvp : kvps.objs()) {
          if (recursiveFlag) {
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
      } else if (methodType == MethodType::DELETE) {
        auto r = cpr::Delete(cpr::Url{kvUrl},
                          cpr::Header{{"Connection", "close"}},
                          cpr::Timeout{1000},
                          cpr::Payload{value},
                          params);
        if (r.status_code != 200) {
          LOG(ERROR) << "consul agent returned error " << r.status_code;
          return EX_TEMPFAIL;
        }
        DLOG_IF(debugFlag, INFO) << "URL: " << r.url;
      } else if (methodType == MethodType::PUT) {
        auto r = cpr::Put(cpr::Url{kvUrl},
                          cpr::Header{{"Connection", "close"}},
                          cpr::Timeout{1000},
                          cpr::Payload{value},
                          params);
        if (r.status_code != 200) {
          LOG(ERROR) << "consul agent returned error " << r.status_code;
          return EX_TEMPFAIL;
        }
        DLOG_IF(debugFlag, INFO) << "URL: " << r.url;
        if (r.text == "true") {
          LOG(INFO) << "Succeeded in posting key \"" << keys.at(0) << "\"";
        } else if (r.text == "false") {
          LOG(WARNING) << "Unable to PUT key \"" << keys.at(0) << "\"";
          return EX_CANTCREAT;
        }
      } else {
        DLOG(FATAL) << "Unsupported methodType";
      }
    }

    return EX_OK;
  } catch (std::exception & e) {
    LOG(FATAL) << "cpr threw an exception: " << e.what();
    return EX_SOFTWARE;
  }

  return EX_OK;
}
