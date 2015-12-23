#ifndef CONSUL_KV_PAIRS_HPP
#define CONSUL_KV_PAIRS_HPP

#include <string>
#include <vector>

#include "b64/decode.hpp"
#include "b64/encode.hpp"
#include "json11.hpp"

#include "consul/kv_pair.hpp"

namespace consul {

class KVPairs final {
public:
  using KVPairsT = std::vector<KVPair>;

  static bool InitFromJson(KVPairs& kvps, std::string json, std::string& err) noexcept {
    auto jr = json11::Json::parse(json, err);
    if (!err.empty()) {
      std::ostringstream ss;
      ss << "Parsing JSON failed: " << err;
      err = ss.str();
      return false;
    }

    if (!jr.is_array()) {
      std::ostringstream ss;
      ss << "Expected array, received " << json11::Json::TypeStr(jr) << " as input.";
      err = ss.str();
      return false;
    }

    const auto arr = jr.array_items();
    for (auto& obj : arr) {
      KVPair kvp;
      if (KVPair::InitFromJson(kvp, obj, err)) {
        kvps.objs_.push_back(kvp);
      } else {
        std::ostringstream ss;
        ss << "Parsing JSON Objects failed: " << err;
        err = ss.str();
        return false;
      }
    }

    return true;
  }

  const KVPairsT& objs() const noexcept { return objs_; }
  KVPairsT::size_type size() const noexcept { return objs_.size(); }
  std::string json() const { return json11::Json(*this).dump(); };
  json11::Json to_json() const { return json11::Json::array{objs_}; }

private:
  KVPairsT objs_;
};

} // namespace consul

#endif // CONSUL_KV_PAIRS_HPP
