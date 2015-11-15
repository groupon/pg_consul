-- Make sure the module is loaded.
-- FIXME(seanc@): this is broken.  Why do I have to call the function to
-- initialize the shared object?  Something's broken here that I don't
-- understand yet and the oversight isn't jumping out at me.  Moving on, but
-- marking this as a bug.
SELECT consul_status_leader();

-- Set agent to localhost
SET consul.agent_hostname = '127.0.0.1';

-- Make sure we only have one peer for the time being
SELECT COUNT(*) FROM consul_status_peers();

-- Grab the current list of peers
SELECT * FROM consul_status_peers() ORDER BY hostname;
