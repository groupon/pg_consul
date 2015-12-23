CREATE EXTENSION pg_consul;

-- Make sure extension parameters are present
SELECT consul_agent_ping(); -- PASS

-- PASS: Decrease the timeout in advance of the following
SET consul.agent_timeout = '10ms';

-- Find the current leader
SELECT consul_agent_ping('127.0.0.1');  -- PASS
SELECT consul_agent_ping('127.0.0.2');  -- FAIL: Doesn't exist

SELECT consul_agent_ping('127.0.0.1', 8500);  -- PASS
SELECT consul_agent_ping('127.0.0.3', 8499);  -- FAIL: Not listen(2)'ing
SELECT consul_agent_ping('127.0.0.4', 8498);  -- FAIL: Not listen(2)'ing
SELECT consul_agent_ping('127.0.0.5', 0);     -- FAIL: Invalid port
SELECT consul_agent_ping('127.0.0.6', -1);    -- FAIL: Invalid port
SELECT consul_agent_ping('127.0.0.7', 65535); -- FAIL: Not listen(2)'ing
SELECT consul_agent_ping('127.0.0.7', 65536); -- FAIL: Invalid port
