* `consul-status`

The following is the output from `consul-status -h`.

```txt
USAGE:

   ./consul-status  [-s=<all|leader|peers>] [-H=<hostname>] [-p=<port>]
                    [-d] [--] [--version] [-h]


Where:

   -s=<all|leader|peers>,  --status=<all|leader|peers>
     Type of status to fetch

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


   consul-status displays the current consul servers (peers) in the consul
   cluster according to the target consul agent.
```
