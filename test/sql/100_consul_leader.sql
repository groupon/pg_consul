-- Make sure the module is loaded.
-- FIXME(seanc@): this is broken.  Why do I have to call the function to
-- initialize the shared object?  Something's broken here that I don't
-- understand yet and the oversight isn't jumping out at me.  Moving on, but
-- marking this as a bug.
SELECT consul_leader();

-- Set hostname to localhost
SET consul.agent_hostname = '127.0.0.1';
SHOW consul.agent_hostname;

-- Find the current leader
SELECT consul_leader();
