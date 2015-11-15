/* pg_consul/pg_consul--0.1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pg_consul" to load this file. \quit

-- Register functions.
CREATE FUNCTION consul_status_leader()
RETURNS TEXT
AS 'MODULE_PATHNAME', 'pg_consul_v1_status_leader'
LANGUAGE C;

CREATE FUNCTION consul_status_peers(
       OUT hostname TEXT,
       OUT port INT2,
       OUT leader BOOL)
RETURNS RECORD
AS 'MODULE_PATHNAME', 'pg_consul_v1_status_peers'
LANGUAGE C;
