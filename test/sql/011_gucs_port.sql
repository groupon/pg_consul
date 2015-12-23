-- Make sure the module is loaded.
--
-- FIXME(seanc@): this is broken.  Why do I have to call the function to
-- initialize the shared object?  WHy isn't _PG_init() called upon new
-- connection from a client?  Something's broken here that I don't understand
-- yet and the oversight isn't jumping out at me.  Moving on, but marking
-- this as a bug.
SELECT consul_agent_ping();

-- PASS: Make sure extension parameters are present
SHOW consul.agent_port;

-- PASS: Set the agent port to something valid according to RFC791.
SET consul.agent_port = '18500';
SHOW consul.agent_port;

-- PASS
SET consul.agent_port = 18501;
SHOW consul.agent_port;

-- FAIL: Set the agent's hostname to something valid according to RFC1123.
SET consul.agent_port = 'fmtp'; -- fmtp 8500/tcp # Flight Message Transfer Protocol
SHOW consul.agent_port;

-- FAIL
SET consul.agent_port = '0';     -- Too small
SHOW consul.agent_port;
-- FAIL
SET consul.agent_port = '65536'; -- Too large
SHOW consul.agent_port;

-- PASS
SET consul.agent_port = 8502;
SHOW consul.agent_port;

-- PASS: Reset
RESET consul.agent_port;
SHOW consul.agent_port;
