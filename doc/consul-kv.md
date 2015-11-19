* consul-kv

The following is the output from `consul-kv -h`.

```txt
USAGE:

   ./consul-kv  {-k=<string> ... |-D=<base64-encoded-string>|-E=<stream of
                bytes>} [-c=<dc1>] [-S=<session>] [-F=<flag>]
                [-C=<modify-index>] [-r] [-v=<value>] [-m=<GET|PUT|DELETE>]
                [-H=<hostname>] [-p=<port>] [-d] [--] [--version] [-h]


Where:

   -k=<string>,  --key=<string>  (accepted multiple times)
     (OR required)  Key(s) operate on with the consul agent
         -- OR --
   -D=<base64-encoded-string>,  --decode=<base64-encoded-string>
     (OR required)  Decode a base64 encoded string
         -- OR --
   -E=<stream of bytes>,  --encode=<stream of bytes>
     (OR required)  Encode a stream of bytes using base64


   -c=<dc1>,  --cluster=<dc1>
     consul Cluster (i.e. '?dc=<cluster>')

   -S=<session>,  --session=<session>
     Acquire a lock using the specified Session

   -F=<flag>,  --flags=<flag>
     Opaque numeric value attached to a key (0 through (2^64)-1)

   -C=<modify-index>,  --cas=<modify-index>
     Check-and-Set index. When performing a PUT or DELETE, only operate if
     the ModifyIndex matches the passed in CAS value

   -r,  --recursive
     Pass the '?recurse' query parameter to recursively find all keys

   -v=<value>,  --value=<value>
     Value to be used when PUT'ing a key (will be base64 encoded
     automatically).

   -m=<GET|PUT|DELETE>,  --method=<GET|PUT|DELETE>
     HTTP method used to act on the key

   -H=<hostname>,  --host=<hostname>
     Hostname of consul agent

   -p=<port>,  --port=<port>
     Port number of consul agent

   -d,  --debug
     Print additional information with debugging

   --,  --ignore_rest
     Ignores the rest of the labeled arguments following this flag.

   --version
     Displays version information and exits.

   -h,  --help
     Displays usage information and exits.


   consul-kv displays all details of a stored key in the consul cluster
   according to the target consul agent.
```
