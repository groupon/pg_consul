#ifndef CONSUL_KV_PAIR_HPP
#define CONSUL_KV_PAIR_HPP

#include <string>

#include "b64/decode.hpp"
#include "b64/encode.hpp"
#include "json11.hpp"

namespace consul {

class KVPair final {
public:
  using KeyT     = std::string;
  using SessionT = std::string;
  using ValueT   = std::string;
  using IndexT   = std::uint64_t;
  using FlagsT   = std::uint64_t;

  static constexpr const char* Base64Header = "-----BEGIN BASE64 ENCODED STREAM-----";
  static constexpr const char* Base64Footer = "-----END BASE64 ENCODED STREAM-----";

  static std::string FlagsStr(FlagsT flags) noexcept {
    std::string str;
    try {
      str = ::boost::lexical_cast<std::string>(flags);
    } catch(const ::boost::bad_lexical_cast &) {
      // Grr... shouldn't happen unless ENOMEM
    }
    return str;
  }

  static std::string IndexStr(IndexT idx) noexcept {
    std::string str;
    try {
      str = ::boost::lexical_cast<std::string>(idx);
    } catch(const ::boost::bad_lexical_cast &) {
      // Grr... shouldn't happen unless ENOMEM
    }
    return str;
  }

  static bool InitFromJson(KVPair& kvp, const json11::Json& obj, std::string& err) noexcept {
    auto objMap = obj.object_items();
    if (objMap.empty()) {
      err = "Unexpected empty object in KV Pair response";
      return false;
    }

    { // CreateIndex
      auto it = objMap.find("CreateIndex");
      if (it != objMap.end()) {
        if (!it->second.is_number()) {
          std::ostringstream ss;
          ss << "CreateIndex's value is not a number: " << json11::Json::TypeStr(it->second);
          err = ss.str();
          return false;
        }

        try {
          kvp.createIndex_ = ::boost::lexical_cast<IndexT>(it->second.int_value());
        } catch(const ::boost::bad_lexical_cast &) {
          std::ostringstream ss;
          ss << "CreateIndex is not a valid Index: " << it->second.dump();
          err = ss.str();
          return false;
        }
      }
    } // CreateIndex

    { // ModifyIndex
      auto it = objMap.find("ModifyIndex");
      if (it != objMap.end()) {
        if (!it->second.is_number()) {
          std::ostringstream ss;
          ss << "ModifyIndex's value is not a number: " << json11::Json::TypeStr(it->second);
          err = ss.str();
          return false;
        }

        try {
          kvp.modifyIndex_ = ::boost::lexical_cast<IndexT>(it->second.int_value());
        } catch(const ::boost::bad_lexical_cast &) {
          std::ostringstream ss;
          ss << "ModifyIndex is not a valid Index: " << it->second.dump();
          err = ss.str();
          return false;
        }
      }
    } // ModifyIndex

    // LockIndex
    {
      auto it = objMap.find("LockIndex");
      if (it != objMap.end()) {
        if (!it->second.is_number()) {
          std::ostringstream ss;
          ss << "LockIndex's value is not a number: " << json11::Json::TypeStr(it->second);
          err = ss.str();
          return false;
        }

        try {
          kvp.lockIndex_ = ::boost::lexical_cast<IndexT>(it->second.int_value());
        } catch(const ::boost::bad_lexical_cast &) {
          std::ostringstream ss;
          ss << "LockIndex is not a valid Index: " << it->second.dump();
          err = ss.str();
          return false;
        }
      }
    } // LockIndex

    // Flags
    {
      auto it = objMap.find("Flags");
      if (it != objMap.end()) {
        if (!it->second.is_number()) {
          std::ostringstream ss;
          ss << "Flags's value is not a number: " << json11::Json::TypeStr(it->second);
          err = ss.str();
          return false;
        }

        try {
          kvp.flags_ = ::boost::lexical_cast<FlagsT>(it->second.int_value());
        } catch(const ::boost::bad_lexical_cast &) {
          std::ostringstream ss;
          ss << "Flags is not valid: " << it->second.dump();
          err = ss.str();
          return false;
        }
      }
    } // Flags

    // Session
    {
      auto it = objMap.find("Session");
      if (it != objMap.end()) {
        if (!it->second.is_string()) {
          std::ostringstream ss;
          ss << "Session's value is not a string: " << typeid(it->second.type()).name();
          err = ss.str();
          return false;
        }

        kvp.session_ = it->second.string_value();
      }
    } // Session

    // Key
    {
      auto it = objMap.find("Key");
      if (it != objMap.end()) {
        if (!it->second.is_string()) {
          std::ostringstream ss;
          ss << "Key's value is not a string: " << typeid(it->second.type()).name();
          err = ss.str();
          return false;
        }

        kvp.key_ = it->second.string_value();
      } else {
        // FIXME(seanc@): To throw, or not throw... that is the question when
        // there's a protocol violation.
      }
    } // Key

    // Value
    {
      auto it = objMap.find("Value");
      if (it != objMap.end()) {
        if (!it->second.is_string()) {
          std::ostringstream ss;
          ss << "Value's value is not a string: " << typeid(it->second.type()).name();
          err = ss.str();
          return false;
        }

        base64::decoder D;
        std::istringstream iss{it->second.string_value()};
        std::ostringstream oss;
        D.decode(iss, oss);
        kvp.value_ = oss.str();
      } else {
        // FIXME(seanc@): To throw, or not throw... that is the question when
        // there's a protocol violation.
      }
    } // Value
    return true;
  }

  IndexT createIndex() const noexcept { return createIndex_; }
  std::string createIndexStr() const noexcept {
    return IndexStr(createIndex_);
  }
  IndexT modifyIndex() const noexcept { return modifyIndex_; }
  std::string modifyIndexStr() const noexcept {
    return IndexStr(modifyIndex_);
  }
  IndexT lockIndex() const noexcept { return lockIndex_; }
  std::string lockIndexStr() const noexcept {
    return IndexStr(lockIndex_);
  }
  FlagsT flags() const noexcept { return flags_; }
  std::string flagsStr() const noexcept {
    return KVPair::FlagsStr(flags_);
  }

  SessionT session() const noexcept { return session_; }
  KeyT key() const noexcept { return key_; }
  ValueT value() const noexcept { return value_; }

  std::string json() const { return json11::Json(*this).dump(); };
  json11::Json to_json() const {
    std::map<KeyT, ValueT> m = {
      { "CreateIndex", ::boost::lexical_cast<ValueT>(createIndex_) },
      { "ModifyIndex", ::boost::lexical_cast<ValueT>(modifyIndex_) },
      { "LockIndex", ::boost::lexical_cast<ValueT>(lockIndex_) },
      { "Flags", ::boost::lexical_cast<ValueT>(flags_) },
      { "Session", session_ },
      { "Key", key_ },
      { "Value", valueEncoded() }
    };

    return json11::Json{m};
  }

  std::string valueEncoded() const noexcept {
    base64::encoder E;
    std::istringstream iss{value_};
    std::ostringstream oss;
    E.encode(iss, oss);

    // Remove trailing newline from encoder
    std::string s = oss.str();
    if (!s.empty() && s[s.length() - 1] == '\n') {
      s.erase(s.length()-1);
    }
    return s;
  }

private:
  IndexT   createIndex_;
  IndexT   modifyIndex_;
  IndexT   lockIndex_;
  FlagsT   flags_;
  KeyT     key_;
  SessionT session_;
  ValueT   value_;
};

} // namespace consul

#endif // CONSUL_KV_PAIR_HPP
