-- Make sure the module is loaded.
--
-- FIXME(seanc@): this is broken.  Why do I have to call the function to
-- initialize the shared object?  WHy isn't _PG_init() called upon new
-- connection from a client?  Something's broken here that I don't understand
-- yet and the oversight isn't jumping out at me.  Moving on, but marking
-- this as a bug.
SELECT consul_agent_ping();

-- PASS: Make sure extension parameters are present
SHOW consul.agent_host;

-- PASS: Set the agent host to something valid according to RFC791.
SET consul.agent_host = '127.0.0.2';
SHOW consul.agent_host;

-- PASS: Set the agent's host to something valid according to RFC1123.
SET consul.agent_host = 'localhost.localdomain';
SHOW consul.agent_host;

-- PASS: Set the agent's host to something valid according to RFC1123.
SET consul.agent_host = 'localhost.localdomain0';
SHOW consul.agent_host;

-- FAIL: Set the agent's host to something valid according to RFC1123.
/* FIXME(seanc@): See FIXME comment in src/pg_consul.cpp, I believe this
 * should be failing. */
SET consul.agent_host = '0localhost.localdomain';
SHOW consul.agent_host;

-- FAIL: Test agent host error handling.
SET consul.agent_host = '.127.0.0.3';
SHOW consul.agent_host;

-- FAIL: Test agent host error handling.
SET consul.agent_host = '127..0.0.4';
SHOW consul.agent_host;

-- FAIL: Test agent host error handling.
SET consul.agent_host = '127.0..0.5';
SHOW consul.agent_host;

-- FAIL: Test agent host error handling.
SET consul.agent_host = '127.0.0..6';
SHOW consul.agent_host;

-- FAIL: Test agent host error handling.
SET consul.agent_host = '127.0.0..7';
SHOW consul.agent_host;

-- FAIL: Test agent host error handling
SET consul.agent_host = '127.0.0.8.';
SHOW consul.agent_host;

-- FAIL: Test agent host error handling
SET consul.agent_host = '127.0.0..9';
SHOW consul.agent_host;

-- FAIL: Don't accept full URLs (yet)
-- FIXME(seanc@): Don't want to deal with parsing yet
SHOW consul.agent_port; -- Test before set
SET consul.agent_host = 'http://127.0.0.5:8501';
SHOW consul.agent_host;
SHOW consul.agent_port;

-- PASS: Return agent to localhost
SET consul.agent_host = '127.0.0.6';
SHOW consul.agent_host;

-- PASS: Return to default
RESET consul.agent_host;
SHOW consul.agent_host;
