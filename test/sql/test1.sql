CREATE EXTENSION pg_consul;

-- Make sure extension parameters are present
SHOW pg_consul.hostname;

-- Set the hostname to something valid according to fetch(3)
SET pg_consul.hostname = 'http://127.0.0.1:8501';
SHOW pg_consul.hostname;

-- Test error handling
SET pg_consul.hostname = 'http://127.0.0.1:-99999';
SHOW pg_consul.hostname;

-- Return to a valid hostname
SET pg_consul.hostname = 'http://127.0.0.1:8500';
SHOW pg_consul.hostname;

-- Find the current leader
SELECT consul_leader();

-- Grab the current list of peers
SELECT * FROM consul_peers() ORDER BY hostname;
