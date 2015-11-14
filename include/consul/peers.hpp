#ifndef CONSUL_PEERS_HPP
#define CONSUL_PEERS_HPP

#include <vector>
#include "json11.hpp"

#include "consul/peer.hpp"

namespace consul {

struct Peers {
  typedef std::vector<::consul::Peer> PeersT;
  PeersT peers;

  static bool InitFromJson(Peers& peers, const std::string& json, std::string& err) {
    auto jr = json11::Json::parse(json, err);
    if (!err.empty()) {
      std::ostringstream ss;
      ss << "Parsing JSON failed: " << err;
      err = ss.str();
      return false;
    }

    if (!jr.is_array()) {
      std::ostringstream ss;
      ss << "Expected array, received ";

      if (jr.is_null()) {           ss << "null";
      } else if (jr.is_number()) {  ss << "number";
      } else if (jr.is_bool()) {    ss << "bool";
      } else if (jr.is_string()) {  ss << "string";
      } else if (jr.is_object()) {  ss << "object";
      } else if (jr.is_array()) {   ss << "array";   // for completeness
      } else {                      ss << "UNKNOWN"; /* lolwut??!! */ }

      ss << " as input.";
      return false;
    }

    auto arr = jr.array_items();
    if (arr.empty()) {
      err = "Unexpected empty array of peers";
      return false;
    }

    for (auto &jsPeer: arr) {
      ::consul::Peer peer;
      if (!::consul::Peer::InitFromJson(peer, jsPeer, err)) {
        std::ostringstream ss;
        ss << "Unable to set peer in peers: " << err;
        err = ss.str();
        return false;
      }
      peers.peers.push_back(peer);
    }

    return true;
  }
};

} // namespace consul

#endif // CONSUL_PEERS_HPP
