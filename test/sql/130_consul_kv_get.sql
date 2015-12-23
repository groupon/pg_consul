-- Make sure the module is loaded.
-- FIXME(seanc@): this is broken.  Why do I have to call the function to
-- initialize the shared object?  Something's broken here that I don't
-- understand yet and the oversight isn't jumping out at me.  Moving on, but
-- marking this as a bug.
SELECT consul_agent_ping();

-- Query values
SELECT * FROM consul_kv_get(key := 'test');
SELECT * FROM consul_kv_get('test');
SELECT * FROM consul_kv_get(key := 'does-not-exist');
SELECT * FROM consul_kv_get(key := 'test', recurse := TRUE);
SELECT * FROM consul_kv_get(key := 'test', recurse := FALSE);
SELECT * FROM consul_kv_get(key := 'test', recurse := TRUE, cluster := 'pgc1');
SELECT * FROM consul_kv_get(key := 'test', recurse := TRUE, cluster := 'pgc1');
SELECT * FROM consul_kv_get(key := 'test', cluster := 'pgc1');
SELECT * FROM consul_kv_get(key := 'test', cluster := 'non-cluster');
SELECT * FROM consul_kv_get(key := 'test/key1');
SELECT key, value, flags, session IS NULL FROM consul_kv_get(key := 'test/key2');
