-- Make sure the module is loaded.
--
-- FIXME(seanc@): this is broken.  Why do I have to call the function to
-- initialize the shared object?  WHy isn't _PG_init() called upon new
-- connection from a client?  Something's broken here that I don't understand
-- yet and the oversight isn't jumping out at me.  Moving on, but marking
-- this as a bug.
SELECT consul_agent_ping();

-- PASS: Make sure extension parameters are present
SHOW consul.agent_timeout;

-- PASS: Set the agent timeout to something valid
SET consul.agent_timeout = 2000;
SHOW consul.agent_timeout;

-- PASS
SET consul.agent_timeout = '3s';
SHOW consul.agent_timeout;

-- PASS
SET consul.agent_timeout = '500ms';
SHOW consul.agent_timeout;

-- FAIL: Test agent hostname error handling
SET consul.agent_timeout = 'in the future';
SHOW consul.agent_timeout;

-- FAIL: Too smail
SET consul.agent_timeout = 0;
SHOW consul.agent_timeout;
-- FAIL: too larrge
SET consul.agent_timeout = '65536ms';
SHOW consul.agent_timeout;
-- PASS: not overly large
SET consul.agent_timeout = '65s';
SHOW consul.agent_timeout;
-- FAIL: too large
SET consul.agent_timeout = '66s';
SHOW consul.agent_timeout;
-- FAIL: too large
SET consul.agent_timeout = '1h';
SHOW consul.agent_timeout;
-- FAIL: too large
SET consul.agent_timeout = '1d';
SHOW consul.agent_timeout;

-- FAIL: Return to a valid timeout
SET consul.agent_timeout = '2.5s';
SHOW consul.agent_timeout;

-- PASS: Return to a valid timeout
SET consul.agent_timeout = '1500ms';
SHOW consul.agent_timeout;

-- PASS: Reset
RESET consul.agent_timeout;
SHOW consul.agent_timeout;
