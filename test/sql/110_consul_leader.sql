-- Make sure the module is loaded.
-- FIXME(seanc@): this is broken.  Why do I have to call the function to
-- initialize the shared object?  Something's broken here that I don't
-- understand yet and the oversight isn't jumping out at me.  Moving on, but
-- marking this as a bug.
SELECT consul_agent_ping();

-- PASS: Find the current leader
SELECT consul_status_leader();
