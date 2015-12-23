/*--------------------------------------------------------------------------
 * pg_consul.cpp - PostgreSQL interface for consul <https://www.consul.io>
 *
 * Copyright (c) 2015, Groupon, Inc.
 *
 * Distributed under the PostgreSQL License.  (See accompanying file LICENSE
 * file or copy at https://github.com/groupon/pg_consul/blob/master/LICENSE)
 * -------------------------------------------------------------------------
 */
extern "C" {
#include "postgres.h"

#include <math.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>

#include "access/hash.h"
#include "executor/instrument.h"
#include "funcapi.h"
#include "mb/pg_wchar.h"
#include "miscadmin.h"
#include "parser/analyze.h"
#include "parser/parsetree.h"
#include "parser/scanner.h"
#include "pgstat.h"
#include "storage/fd.h"
#include "storage/ipc.h"
#include "storage/spin.h"
#include "tcop/utility.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
} // extern "C"

#include <iostream>
#include <regex>
#include <sstream>
#include <string>

#include "cpr/cpr.h"
#include "json11.hpp"
#include "boost/algorithm/string.hpp"
#include "consul.hpp"

extern "C" {
#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

// ---- Exported function decls that need to be visible after the .so is dlopen()'ed.
void _PG_init(void);
void _PG_fini(void);
PG_FUNCTION_INFO_V1(pg_consul_v1_agent_ping0);
PG_FUNCTION_INFO_V1(pg_consul_v1_agent_ping2);
PG_FUNCTION_INFO_V1(pg_consul_v1_kv_get);
PG_FUNCTION_INFO_V1(pg_consul_v1_status_leader);
PG_FUNCTION_INFO_V1(pg_consul_v1_status_peers);
} // extern "C"

namespace {
// ---- pg_consul-specific structs

// consul_kv_get() function context
struct ConsulGetFctx {
  ::consul::KVPairs kvps;
  ::consul::KVPairs::KVPairsT::size_type iter = 0;
};

// consul_status_peers() function context
struct ConsulPeersFctx {
  ::consul::Peers peers;
  ::consul::Peers::PeersT::size_type iter = 0;
};

// ---- Constants
static const constexpr ::consul::Agent::PortT PG_CONSUL_AGENT_PORT_DEFAULT = 8500;
static const constexpr char PG_CONSUL_AGENT_HOST_DEFAULT[] = "127.0.0.1";
static const constexpr char PG_CONSUL_AGENT_HOST_LONG_DESCR[] = "Host of the consul agent this API client should use to talk with";
static const constexpr char PG_CONSUL_AGENT_HOST_SHORT_DESCR[] = "Sets host of the consul agent to talk to.";
static const constexpr char PG_CONSUL_AGENT_PORT_LONG_DESCR[] = "Port number of the consul agent this API client should use to talk with";
static const constexpr char PG_CONSUL_AGENT_PORT_SHORT_DESCR[] = "Port number used by the agent for consul RPC requests.";
static const char PG_CONSUL_AGENT_TIMEOUT_LONG_DESCR[] = "Timeout (ms) used when communicating with consul agent.";
static const char PG_CONSUL_AGENT_TIMEOUT_SHORT_DESCR[] = "Timeout (ms) for communicating with consul agent";

// RFC 1123 says names must be shorter than 255.
static const constexpr auto RFC1123_NAME_LIMIT = 255;

// -- consul_peers() SETOF column constants
static const constexpr int PG_CONSUL_PEERS1_COLUMN_HOST   = 0;
static const constexpr int PG_CONSUL_PEERS1_COLUMN_PORT   = 1;
static const constexpr int PG_CONSUL_PEERS1_COLUMN_LEADER = 2;
static const constexpr int PG_CONSUL_PEERS1_NUM_COLUMNS   = 3;

// -- consul_kv_get() SETOF column constants
// {"CreateIndex": "469", "Flags": "0", "Key": "test", "LockIndex": "0", "ModifyIndex": "469", "Session": "", "Value": "dGVzdC12YWx1ZQ=="}
static const constexpr int PG_CONSUL_KV1_GET_IN_KEY_POS       = 0;
static const constexpr int PG_CONSUL_KV1_GET_IN_RECURSE_POS   = 1;
static const constexpr int PG_CONSUL_KV1_GET_IN_CLUSTER_POS   = 2;
static const constexpr int PG_CONSUL_KV1_GET_COUMN_KEY        = 0;
static const constexpr int PG_CONSUL_KV1_GET_COUMN_VALUE      = 1;
static const constexpr int PG_CONSUL_KV1_GET_COUMN_FLAGS      = 2;
static const constexpr int PG_CONSUL_KV1_GET_COUMN_CREATE_IDX = 3;
static const constexpr int PG_CONSUL_KV1_GET_COUMN_MODIFY_IDX = 4;
static const constexpr int PG_CONSUL_KV1_GET_COUMN_LOCK_IDX   = 5;
static const constexpr int PG_CONSUL_KV1_GET_COUMN_SESSION    = 6;
static const constexpr int PG_CONSUL_KV1_GET_NUM_COLUMNS      = 7;

// ---- GUC variables

// NOTE: this variable still needs to be defined even though the
// authoritative value is contained within pgConsulAgent.
static int pg_consul_agent_port = PG_CONSUL_AGENT_PORT_DEFAULT;
static char* pg_consul_agent_host_string = nullptr;
static int pg_consul_agent_timeout_ms = 0;
static ::consul::Agent pgConsulAgent;

// ---- Function declarations
static void pg_consul_agent_port_assign_hook(int newvalue, void *extra);
static       void  pg_consul_agent_host_assign_hook(const char *newvalue, void *extra);
static       bool  pg_consul_agent_host_check_hook(const char *newval);
static       bool  pg_consul_agent_host_check_hook(char **newval, void **extra, GucSource source);
static const char* pg_consul_agent_host_show_hook(void);
static const char* pg_consul_agent_timeout_show_hook(void);
static       void  pg_consul_agent_timeout_assign_hook(int newvalue, void *extra);
static const char* pg_consul_agent_timeout_show_hook(void);
} // anon-namespace

extern "C" {

/*
 * Module load callback
 */
void
_PG_init(void)
{
  // Set defaults
  pg_consul_agent_host_string = const_cast<char*>(consul::Agent::DEFAULT_HOST);

  // Define (or redefine) custom GUC variables.
  DefineCustomStringVariable("consul.agent_host",
                             PG_CONSUL_AGENT_HOST_SHORT_DESCR,
                             PG_CONSUL_AGENT_HOST_LONG_DESCR,
                             &pg_consul_agent_host_string,
                             PG_CONSUL_AGENT_HOST_DEFAULT,
                             PGC_USERSET,
                             GUC_NOT_WHILE_SEC_REST,
                             pg_consul_agent_host_check_hook,
                             pg_consul_agent_host_assign_hook,
                             pg_consul_agent_host_show_hook);

  DefineCustomIntVariable("consul.agent_port",
                          PG_CONSUL_AGENT_PORT_SHORT_DESCR,
                          PG_CONSUL_AGENT_PORT_LONG_DESCR,
                          &pg_consul_agent_port,
                          PG_CONSUL_AGENT_PORT_DEFAULT,
                          1024,  // Min port == 1024
                          65535, // Max port == 65536 - 1
                          PGC_USERSET,
                          GUC_LIST_INPUT,
                          nullptr,
                          pg_consul_agent_port_assign_hook,
                          nullptr
                          );
  DefineCustomIntVariable("consul.agent_timeout",
                          PG_CONSUL_AGENT_TIMEOUT_SHORT_DESCR,
                          PG_CONSUL_AGENT_TIMEOUT_LONG_DESCR,
                          &pg_consul_agent_timeout_ms,
                          consul::Agent::DEFAULT_TIMEOUT_MS,
                          consul::Agent::DEFAULT_TIMEOUT_MS_MIN,
                          consul::Agent::DEFAULT_TIMEOUT_MS_MAX,
                          PGC_USERSET,
                          GUC_UNIT_MS | GUC_NOT_WHILE_SEC_REST,
                          nullptr,
                          pg_consul_agent_timeout_assign_hook,
                          pg_consul_agent_timeout_show_hook);

  EmitWarningsOnPlaceholders("consul");
}


/*
 * Module unload callback
 */
void
_PG_fini(void)
{
  // Uninstall hooks.
}


Datum
pg_consul_v1_agent_ping0(PG_FUNCTION_ARGS) {
  try {
    auto selfUrl = pgConsulAgent.selfUrl();
    auto r = cpr::Get(cpr::Url{selfUrl},
                      cpr::Header{{"Connection", "close"}},
                      cpr::Timeout{pgConsulAgent.timeoutMs()});
    if (r.status_code == 200) {
      return true;
    } else {
      return false;
    }
  } catch (std::exception & e) {
    return false;
  }
}


Datum
pg_consul_v1_agent_ping2(PG_FUNCTION_ARGS) {
  try {
    consul::Peer::HostT host{VARDATA(PG_GETARG_TEXT_P(0))};
    consul::Peer::PortT port = PG_GETARG_INT32(1); // FIXME(seanc@): int32_t -> uint16_t narrowing
    consul::Agent localAgent{host, port};

    const auto selfUrl = localAgent.selfUrl();
    const auto timeout = localAgent.timeoutMs();
    auto r = cpr::Get(cpr::Url{selfUrl},
                      cpr::Header{{"Connection", "close"}},
                      cpr::Timeout{timeout});
    if (r.status_code == 200) {
      return true;
    } else {
      return false;
    }

    // Check to see if Server is true
  } catch (std::exception & e) {
    return false;
  }
}


Datum
pg_consul_v1_kv_get(PG_FUNCTION_ARGS) {
  MemoryContext oldcontext;
  void* fctx_p;
  TupleDesc tupdesc;
  AttInMetadata *attinmeta;
  ConsulGetFctx *fctx;
  FuncCallContext *funcctx;
  uint32 call_cntr;
  uint32 max_calls;

  if (SRF_IS_FIRSTCALL()) {
    // create a function context for cross-call persistence
    funcctx = SRF_FIRSTCALL_INIT();

    // switch to memory context appropriate for multiple function calls
    oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

    // Build a tuple descriptor for our result type
    if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
      ereport(ERROR,
              (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
               errmsg("function returning record called in context "
                      "that cannot accept type record")));

    // generate attribute metadata needed later to produce tuples from raw C
    // strings
    attinmeta = TupleDescGetAttInMetadata(tupdesc);
    funcctx->attinmeta = attinmeta;

    // allocate memory for user context.  Use emlacement new operator.
    fctx_p = (ConsulGetFctx *)palloc(sizeof(ConsulGetFctx));

    /*
     * Use fctx to keep track of KVPair entries from call to call.  The first
     * call returns the entire list, then every subsequent call iterates
     * through the list.
     */
    ConsulGetFctx* fctx = new(fctx_p) ConsulGetFctx();

    // WARNING(seanc@): it is exceedingly important to destruct the object
    // explicitly: fctx->~ConsulGetFctx(); Pulling this off requires a bit
    // of manual accountancy.  If the query is cancelled while returning
    // rows, this will leak because the destructor is *ONLY* called when
    // call_cntr == max_calls.
    funcctx->user_fctx = fctx;

    // Populate KVPairs via cpr
    try {
      consul::KVPair::KeyT key;
      if (PG_ARGISNULL(PG_CONSUL_KV1_GET_IN_KEY_POS)) {
        PG_RETURN_NULL();
      } else {
        text *keyp = PG_GETARG_TEXT_P(PG_CONSUL_KV1_GET_IN_KEY_POS);
        key.assign(VARDATA(keyp), VARSIZE(keyp) - VARHDRSZ);
      }

      bool recurseParam = false;
      if (!PG_ARGISNULL(PG_CONSUL_KV1_GET_IN_RECURSE_POS))
        recurseParam = PG_GETARG_BOOL(PG_CONSUL_KV1_GET_IN_RECURSE_POS);

      consul::Agent::ClusterT dcParam;
      if (!PG_ARGISNULL(PG_CONSUL_KV1_GET_IN_CLUSTER_POS)) {
        text *clusterp = PG_GETARG_TEXT_P(PG_CONSUL_KV1_GET_IN_CLUSTER_POS);
        dcParam.assign(VARDATA(clusterp), VARSIZE(clusterp) - VARHDRSZ);
      }

      const consul::KVPair::IndexT casParam = 0;
      const consul::KVPair::SessionT acquireParam;
      const consul::KVPair::FlagsT flagsParam = 0;

      auto params = cpr::Parameters();
      if (!dcParam.empty()) {
        params.AddParameter({"dc", dcParam});
      }

      if (recurseParam) {
        params.AddParameter({"recurse", ""});
      }

      if (casParam) {
        params.AddParameter({"cas", consul::KVPair::IndexStr(casParam)});
      }

      if (flagsParam) {
        params.AddParameter({"flags", consul::KVPair::FlagsStr(flagsParam)});
      }

      if (!acquireParam.empty()) {
        params.AddParameter({"acquire", acquireParam});
      }

      // Make a call to get the current leader
      auto kvUrl = pgConsulAgent.kvUrl(key);
      auto r = cpr::Get(cpr::Url{kvUrl},
                        cpr::Header{{"Connection", "close"}},
                        cpr::Timeout{pgConsulAgent.timeoutMs()},
                        params);
      if (r.status_code != 200) {
        ereport(ERROR,
                (errcode(ERRCODE_FDW_UNABLE_TO_ESTABLISH_CONNECTION),
                 errmsg("consul_kv_get() returned error %ld", r.status_code)));
      }

      consul::KVPairs kvps;
      std::string err;
      if (!::consul::KVPairs::InitFromJson(fctx->kvps, r.text, err)) {
        ereport(ERROR, (errcode(ERRCODE_FDW_REPLY_HANDLE),
                        errmsg("Failed to load KV pairs from JSON: %s: %s", err.c_str(), r.text.c_str())));
      }

      if (!recurseParam && fctx->kvps.size() > 1) {
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("consul_kv_get() performed a non-recursive GET but received %lu responses", fctx->kvps.size())));
      }

      // Set the max calls
      funcctx->max_calls = fctx->kvps.objs().size();
    } catch (std::exception & e) {
      ereport(ERROR,
              (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
               errmsg("consul_kv_get() failed: %s", std::string(e.what()).c_str())));
    }

    MemoryContextSwitchTo(oldcontext);
  }

  // Stuff done on every call of the function
  funcctx = SRF_PERCALL_SETUP();

  call_cntr = funcctx->call_cntr;
  max_calls = funcctx->max_calls;
  attinmeta = funcctx->attinmeta;
  fctx = (ConsulGetFctx*)funcctx->user_fctx;

  if (call_cntr < max_calls) { // do when there is more left to send
    char       **values;
    HeapTuple    tuple;
    Datum        result;

    // Snag a reference to the KVPair we're going to send back
    const auto& objs = fctx->kvps.objs();
    const auto& kvp = objs[fctx->iter];
    fctx->iter++;

    // Prepare a values array for building the returned tuple.  This should
    // be an array of C strings which will be processed later by the type
    // input functions.
    values = (char **)palloc(PG_CONSUL_KV1_GET_NUM_COLUMNS * sizeof(char *));
    // PG_CONSUL_KV1_GET_COUMN_KEY == key (TEXT)
    values[PG_CONSUL_KV1_GET_COUMN_KEY] = (char *)palloc(kvp.key().size() + 1);
    kvp.key().copy(values[PG_CONSUL_KV1_GET_COUMN_KEY], kvp.key().size());
    values[PG_CONSUL_KV1_GET_COUMN_KEY][kvp.key().size()] = '\0';
    // PG_CONSUL_KV1_GET_COUMN_VALUE == value (BYTEA)
    values[PG_CONSUL_KV1_GET_COUMN_VALUE] = (char *)palloc(kvp.value().size() + 1);
    kvp.value().copy(values[PG_CONSUL_KV1_GET_COUMN_VALUE], kvp.value().size());
    values[PG_CONSUL_KV1_GET_COUMN_VALUE][kvp.value().size()] = '\0';
    // PG_CONSUL_KV1_GET_COUMN_FLAGS == flags (INT)
    const auto flagsStr = kvp.flagsStr();
    values[PG_CONSUL_KV1_GET_COUMN_FLAGS] = (char *)palloc(flagsStr.size() + 1);
    flagsStr.copy(values[PG_CONSUL_KV1_GET_COUMN_FLAGS], flagsStr.size());
    values[PG_CONSUL_KV1_GET_COUMN_FLAGS][flagsStr.size()] = '\0';
    // PG_CONSUL_KV1_GET_COUMN_CREATE_IDX == create_index (INT8)
    const auto createIndexStr = kvp.createIndexStr();
    values[PG_CONSUL_KV1_GET_COUMN_CREATE_IDX] = (char *)palloc(createIndexStr.size() + 1);
    createIndexStr.copy(values[PG_CONSUL_KV1_GET_COUMN_CREATE_IDX], createIndexStr.size());
    values[PG_CONSUL_KV1_GET_COUMN_CREATE_IDX][createIndexStr.size()] = '\0';
    // PG_CONSUL_KV1_GET_COUMN_LOCK_IDX == lock_index (INT8)
    const auto lockIndexStr = kvp.lockIndexStr();
    values[PG_CONSUL_KV1_GET_COUMN_LOCK_IDX] = (char *)palloc(lockIndexStr.size() + 1);
    lockIndexStr.copy(values[PG_CONSUL_KV1_GET_COUMN_LOCK_IDX], lockIndexStr.size());
    values[PG_CONSUL_KV1_GET_COUMN_LOCK_IDX][lockIndexStr.size()] = '\0';
    // PG_CONSUL_KV1_GET_COUMN_MODIFY_IDX == modify_index (INT8)
    const auto modifyIndexStr = kvp.modifyIndexStr();
    values[PG_CONSUL_KV1_GET_COUMN_MODIFY_IDX] = (char *)palloc(modifyIndexStr.size() + 1);
    modifyIndexStr.copy(values[PG_CONSUL_KV1_GET_COUMN_MODIFY_IDX], modifyIndexStr.size());
    values[PG_CONSUL_KV1_GET_COUMN_MODIFY_IDX][modifyIndexStr.size()] = '\0';
    // PG_CONSUL_KV1_GET_COUMN_SESSION == session (TEXT)
    values[PG_CONSUL_KV1_GET_COUMN_SESSION] = (char *)palloc(kvp.session().size() + 1);
    kvp.session().copy(values[PG_CONSUL_KV1_GET_COUMN_SESSION], kvp.session().size());
    values[PG_CONSUL_KV1_GET_COUMN_SESSION][kvp.session().size()] = '\0';

    /* build a tuple */
    tuple = BuildTupleFromCStrings(attinmeta, values);

    /* make the tuple into a datum */
    result = HeapTupleGetDatum(tuple);

    /* clean up (this is not really necessary) */
    for (auto i = 0; i < PG_CONSUL_KV1_GET_NUM_COLUMNS; ++i) {
      pfree(values[i]);
    }

    SRF_RETURN_NEXT(funcctx, result);
  } else {
    // Do when there is no more left
    fctx->~ConsulGetFctx();
    SRF_RETURN_DONE(funcctx);
  }
}



/*
 * Obtain the current leader of the Raft quorum
 */
Datum
pg_consul_v1_status_leader(PG_FUNCTION_ARGS)
{
  using json11::Json;

  try {
    auto r = cpr::Get(cpr::Url{pgConsulAgent.statusLeaderUrl()},
                      cpr::Header{{"Connection", "close"}},
                      cpr::Timeout{pgConsulAgent.timeoutMs()});
    if (r.status_code != 200) {
      ereport(ERROR,
              (errcode(ERRCODE_FDW_UNABLE_TO_ESTABLISH_CONNECTION),
               errmsg("consul_status_leader() returned error %ld", r.status_code)));
    }

    if (r.text.size() == 0) {
      PG_RETURN_NULL();
    }

    consul::Peer leader;
    std::string err;
    if (!::consul::Peer::InitFromJson(leader, r.text, err)) {
      ereport(ERROR, (errcode(ERRCODE_FDW_REPLY_HANDLE),
                      errmsg("Failed to load leader from JSON: %s", err.c_str())));
    }

    PG_RETURN_TEXT_P(cstring_to_text(leader.str().c_str()));
  } catch (std::exception & e) {
    ereport(ERROR,
            (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
             errmsg("consul_status_leader() failed: %s", std::string(e.what()).c_str())));
  }
}


Datum
pg_consul_v1_status_peers(PG_FUNCTION_ARGS) {
  MemoryContext oldcontext;
  void* fctx_p;
  TupleDesc tupdesc;
  AttInMetadata *attinmeta;
  ConsulPeersFctx *fctx;
  FuncCallContext *funcctx;
  uint32 call_cntr;
  uint32 max_calls;

  if (SRF_IS_FIRSTCALL()) {
    // create a function context for cross-call persistence
    funcctx = SRF_FIRSTCALL_INIT();

    // switch to memory context appropriate for multiple function calls
    oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

    // Build a tuple descriptor for our result type
    if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
      ereport(ERROR,
              (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
               errmsg("function returning record called in context "
                      "that cannot accept type record")));

    // generate attribute metadata needed later to produce tuples from raw C
    // strings
    attinmeta = TupleDescGetAttInMetadata(tupdesc);
    funcctx->attinmeta = attinmeta;

    // allocate memory for user context.  Use emlacement new operator.
    fctx_p = (ConsulPeersFctx *)palloc(sizeof(ConsulPeersFctx));

    /*
     * Use fctx to keep track of peers list from call to call.  The first
     * call returns the entire list, then every subsequent call iterates
     * through the list.
     */
    ConsulPeersFctx* fctx = new(fctx_p) ConsulPeersFctx();

    // WARNING(seanc@): it is exceedingly important to destruct the object
    // explicitly: fctx->~ConsulPeersFctx(); Pulling this off requires a bit
    // of manual accountancy.  If the query is cancelled while returning
    // rows, this will leak because the destructor is *ONLY* called when
    // call_cntr == max_calls.
    funcctx->user_fctx = fctx;

    // Populate our peers list via cpr call
    try {
      // Make a call to get the current leader
      auto r = cpr::Get(cpr::Url{pgConsulAgent.statusLeaderUrl()},
                        cpr::Header{{"Connection", "close"}},
                        cpr::Timeout{pgConsulAgent.timeoutMs()});
      if (r.status_code != 200) {
        ereport(ERROR,
                (errcode(ERRCODE_FDW_UNABLE_TO_ESTABLISH_CONNECTION),
                 errmsg("consul_status_leader() returned error %ld", r.status_code)));
      }

      consul::Peer leader;
      std::string err;
      if (!::consul::Peer::InitFromJson(leader, r.text, err)) {
        ereport(ERROR, (errcode(ERRCODE_FDW_REPLY_HANDLE),
                        errmsg("Failed to load leader from JSON: %s", err.c_str())));
      }

      // Then query the current list of peers
      r = cpr::Get(cpr::Url{pgConsulAgent.statusPeersUrl()},
                        cpr::Header{{"Connection", "close"}},
                   cpr::Timeout{pgConsulAgent.timeoutMs()});
      if (r.status_code != 200) {
        ereport(ERROR,
                (errcode(ERRCODE_FDW_UNABLE_TO_ESTABLISH_CONNECTION),
                 errmsg("consul_status_peers() returned error %ld", r.status_code)));
      }

      if (!::consul::Peers::InitFromJson(fctx->peers, r.text, err)) {
        ereport(ERROR, (errcode(ERRCODE_FDW_REPLY_HANDLE),
                        errmsg("Failed to load peers from JSON: %s: %s", err.c_str(), r.text.c_str())));
      }

      // Set the peer who is the leader with the leader bit
      for (auto& peer : fctx->peers.peers) {
        if (peer == leader) {
          peer.leader = true;
          break;
        }
      }

      // Set the max calls
      funcctx->max_calls = fctx->peers.peers.size();
    } catch (std::exception & e) {
      ereport(ERROR,
              (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
               errmsg("consul_status_peers() failed: %s", std::string(e.what()).c_str())));
    }

    MemoryContextSwitchTo(oldcontext);
  }

  // Stuff done on every call of the function
  funcctx = SRF_PERCALL_SETUP();

  call_cntr = funcctx->call_cntr;
  max_calls = funcctx->max_calls;
  attinmeta = funcctx->attinmeta;
  fctx = (ConsulPeersFctx*)funcctx->user_fctx;

  if (call_cntr < max_calls) { // do when there is more left to send
    char       **values;
    HeapTuple    tuple;
    Datum        result;

    // Snag a reference to the peer we're going to send back
    const auto& peer = fctx->peers.peers[fctx->iter];
    fctx->iter++;

    // Prepare a values array for building the returned tuple.  This should
    // be an array of C strings which will be processed later by the type
    // input functions.
    values = static_cast<char **>(palloc(PG_CONSUL_PEERS1_NUM_COLUMNS * sizeof(char *)));
    // PG_CONSUL_PEERS1_COLUMN_HOST == host (TEXT)
    values[PG_CONSUL_PEERS1_COLUMN_HOST] = static_cast<char *>(palloc(peer.host.size() + 1));
    peer.host.copy(values[PG_CONSUL_PEERS1_COLUMN_HOST], peer.host.size());
    values[PG_CONSUL_PEERS1_COLUMN_HOST][peer.host.size()] = '\0';
    // PG_CONSUL_PEERS1_COLUMN_PORT == port number (INT4)
    const auto portStr = peer.portStr();
    values[PG_CONSUL_PEERS1_COLUMN_PORT] = (char *)palloc(portStr.size() + 1);
    portStr.copy(values[PG_CONSUL_PEERS1_COLUMN_PORT], portStr.size());
    values[PG_CONSUL_PEERS1_COLUMN_PORT][portStr.size()] = '\0';
    // PG_CONSUL_PEERS1_COLUMN_LEADER == leader state (BOOL).  bool as
    // represented by a char.  String literals are null terminated.
    values[PG_CONSUL_PEERS1_COLUMN_LEADER] = (char *)palloc(sizeof("f"));
    values[PG_CONSUL_PEERS1_COLUMN_LEADER][0] = (peer.leader ? 't' : 'f');
    values[PG_CONSUL_PEERS1_COLUMN_LEADER][1] = '\0';

    /* build a tuple */
    tuple = BuildTupleFromCStrings(attinmeta, values);

    /* make the tuple into a datum */
    result = HeapTupleGetDatum(tuple);

    /* clean up (this is not really necessary) */
    for (auto i = 0; i < PG_CONSUL_PEERS1_NUM_COLUMNS; ++i) {
      pfree(values[i]);
    }

    SRF_RETURN_NEXT(funcctx, result);
  } else {
    // Do when there is no more left
    fctx->~ConsulPeersFctx();
    SRF_RETURN_DONE(funcctx);
  }
}

} // extern "C"

namespace {

static void
pg_consul_agent_host_assign_hook(const char *newHost, void *extra) {
  // FIXME(seanc@): This is pretty dumb.  By API design we're compelled to
  // const_cast<> from const char*, but in DefineCustomStringVariable() we
  // can't pass nullptr for either the assignment when the function pointer
  // is not null.  If you pass a *_assign_hook() function pointer to
  // DefineCustom*Variable(), you can only pass a char* as an argument.  It
  // feels like I'm missing something obvious.
  pg_consul_agent_host_string = const_cast<char*>(newHost);
  pgConsulAgent.setHost(newHost);
}

static bool
pg_consul_agent_host_check_hook(char **newHost, void **extra, GucSource source) {
  if (newHost == nullptr || *newHost == nullptr) {
    return false;
  } else {
    return pg_consul_agent_host_check_hook(*newHost);
  }
}


static bool
pg_consul_agent_host_check_hook(const char *newHost) {
  const auto rfc791RegexPat = u8R"regex(^(([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])$)regex";
  const auto rfc1123RegexPat = u8R"regex(^(([a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\-]*[a-zA-Z0-9])\.)*([A-Za-z0-9]|[A-Za-z0-9][A-Za-z0-9\-]*[A-Za-z0-9])$)regex";

  std::regex rfc791Regex(rfc791RegexPat);   // IPv4
  std::regex rfc1123Regex(rfc1123RegexPat); // Host

  const std::string newHostStr(newHost);
  if (newHostStr.size() > RFC1123_NAME_LIMIT) {
    // Not testing to see if the length of labels are shorter than 63 bytes.
    return false;
  }

  bool validIp = false, validHost = false;
  validIp = std::regex_match(newHostStr, rfc791Regex);
  if (!validIp) {
    validHost = std::regex_match(newHostStr, rfc1123Regex);
  }

  return (validIp || validHost);
}

static const char*
pg_consul_agent_host_show_hook(void) {
  return pg_consul_agent_host_string;
  //  return pgConsulAgent.host().c_str();
}

static void
pg_consul_agent_port_assign_hook(const int newPort, void *extra) {
  pg_consul_agent_port = static_cast<std::uint16_t>(newPort);
  pgConsulAgent.setPort(newPort);
}

static void
pg_consul_agent_timeout_assign_hook(int newvalue, void *extra) {
  pgConsulAgent.setTimeoutMs(newvalue);
}


static const char*
pg_consul_agent_timeout_show_hook(void) {
  return pgConsulAgent.timeoutStr().c_str();
}

} // anon-namespace
