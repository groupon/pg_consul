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
PG_FUNCTION_INFO_V1(pg_consul_v1_status_leader);
PG_FUNCTION_INFO_V1(pg_consul_v1_status_peers);
} // extern "C"

namespace {
// ---- pg_consul-specific structs

// consul_status_peers() function context
struct ConsulPeersFctx {
  ::consul::Peers peers;
  ::consul::Peers::PeersT::size_type iter = 0;
};

// ---- Constants
static const constexpr char PG_CONSUL_AGENT_HOSTNAME_DEFAULT[] = "127.0.0.1";
static const constexpr char PG_CONSUL_AGENT_HOSTNAME_LONG_DESCR[] = "Hostname of the consul agent this API client should use to talk with";
static const constexpr char PG_CONSUL_AGENT_HOSTNAME_SHORT_DESCR[] = "Sets hostname of the consul agent to talk to.";
static const constexpr ::consul::Agent::PortT PG_CONSUL_AGENT_PORT_DEFAULT = 8500;
static const constexpr char PG_CONSUL_AGENT_PORT_LONG_DESCR[] = "Port number of the consul agent this API client should use to talk with";
static const constexpr char PG_CONSUL_AGENT_PORT_SHORT_DESCR[] = "Port number used by the agent for consul RPC requests.";

// ---- GUC variables

// NOTE: this variable still needs to be defined even though the
// authoritative value is contained within pgConsulAgent.
static char* pg_consul_agent_hostname_string = NULL;
static int pg_consul_agent_port = PG_CONSUL_AGENT_PORT_DEFAULT;
::consul::Agent pgConsulAgent;

// ---- Function declarations
static void pg_consul_agent_hostname_assign_hook(const char *newvalue, void *extra);
static bool pg_consul_agent_hostname_check_hook(char **newval, void **extra, GucSource source);
static const char* pg_consul_agent_hostname_show_hook(void);
static void pg_consul_agent_port_assign_hook(int newvalue, void *extra);
} // anon-namespace

extern "C" {

/*
 * Module load callback
 */
void
_PG_init(void)
{
  /*
   * Define (or redefine) custom GUC variables.
   */
  DefineCustomStringVariable("consul.agent_hostname",
                             PG_CONSUL_AGENT_HOSTNAME_SHORT_DESCR,
                             PG_CONSUL_AGENT_HOSTNAME_LONG_DESCR,
                             &pg_consul_agent_hostname_string,
                             PG_CONSUL_AGENT_HOSTNAME_DEFAULT,
                             PGC_USERSET,
                             GUC_LIST_INPUT,
                             pg_consul_agent_hostname_check_hook,
                             pg_consul_agent_hostname_assign_hook,
                             pg_consul_agent_hostname_show_hook);

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
                      cpr::Timeout{1000});
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
    fctx_p = (ConsulPeersFctx *) palloc(sizeof(ConsulPeersFctx));

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
                        cpr::Timeout{1000});
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
                        cpr::Timeout{1000});
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
    values = (char **) palloc(3 * sizeof(char *));
    // Column 0 == hostname (TEXT)
    values[0] = (char *) palloc(peer.host.length() + 1);
    // Column 1 == port number (INT2)
    const auto portStr = peer.portStr();
    values[1] = (char *) palloc(portStr.size() + 1);
    // Column 2 == leader state (BOOL)
    values[2] = (char *) palloc(sizeof("f")); // bool as represented by a
                                              // char.  String literals are
                                              // null terminated.

    // Set hostname
    peer.host.copy(values[0], peer.host.size());
    values[0][peer.host.size()] = '\0';

    // Set port
    portStr.copy(values[1], portStr.size());
    values[1][portStr.size()] = '\0';

    // Set bool
    values[2][0] = (peer.leader ? 't' : 'f');
    values[2][1] = '\0';

    /* build a tuple */
    tuple = BuildTupleFromCStrings(attinmeta, values);

    /* make the tuple into a datum */
    result = HeapTupleGetDatum(tuple);

    /* clean up (this is not really necessary) */
    pfree(values[0]);
    pfree(values[1]);
    pfree(values[2]);
    pfree(values);

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
pg_consul_agent_hostname_assign_hook(const char *newHostname, void *extra) {
  pg_consul_agent_hostname_string = const_cast<char*>(newHostname); // This is pretty dumb.
  pgConsulAgent.setHost(newHostname);
}

static bool
pg_consul_agent_hostname_check_hook(char **newHostname, void **extra, GucSource source) {
  if (newHostname == nullptr || *newHostname == nullptr)
    return false;

  const auto rfc791RegexPat = u8R"regex(^(([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])$)regex";
  const auto rfc1123RegexPat = u8R"regex(^(([a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\-]*[a-zA-Z0-9])\.)*([A-Za-z0-9]|[A-Za-z0-9][A-Za-z0-9\-]*[A-Za-z0-9])$)regex";

  std::regex rfc791Regex(rfc791RegexPat);   // IPv4
  std::regex rfc1123Regex(rfc1123RegexPat); // Hostname

  const std::string newHostnameStr(*newHostname);
  if (newHostnameStr.size() > 255) {
    // RFC 1123 says names must be shorter than 255.  Not testing to see if
    // the length of labels are shorter than 63 bytes.
    return false;
  }

  bool validIp = false, validHostname = false;
  validIp = std::regex_match(newHostnameStr, rfc791Regex);
  if (!validIp) {
    validHostname = std::regex_match(newHostnameStr, rfc1123Regex);
  }

  return (validIp || validHostname);
}

static const char*
pg_consul_agent_hostname_show_hook(void) {
  return pgConsulAgent.host().c_str();
}

static void
pg_consul_agent_port_assign_hook(const int newPort, void *extra) {
  pg_consul_agent_port = static_cast<std::uint16_t>(newPort);
  pgConsulAgent.setPort(newPort);
}

} // anon-namespace
