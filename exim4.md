# Using mailquotad with exim #

[Exim](http://exim.org) has no builtin facility for checking the quotas of a mailbox at RCPT TO time. And when you have many MX servers and only one mail hub that stores mail data it became a real problem. Say, MX server receives mail, but mailhub defers it because recipient's mailbox is full. But MX already has said to remote server that mail had been successfully delivered. So I have written program (now it is perl script) that is daemon, it creates socket and listen for text message that consists of local\_part, domain and quota values delimited by colon. If mailbox is full daemon writes back word _bad_ else _good_ and than close connection.

# Details #

For **mailquotad**  in tar.gz archive default settings and daemon's rc.d script are suitable for [FreeBSD](http://www.freebsd.org/) and there is a RPM package for RedHat Linux. For other OSes, please, check if default values fit to your system.
**mailquotad** supports some cli options:
```
-h this help message
-d <maildir> run in debug mode, do not became daemon
-s <UNIX socket path|IP address:port>
-p <pid file path>
-u <user>
-g <group>
-m <mail spool prefix>
```

To check quota in exim you should add to your exim's acl rules something like this:
```
acl_check_rcpt:
#
# ...
#
        deny    message       = e-mail address [$local_part@$domain] doesn't exist or blocked or mailbox is full
                domains       = +local_domains
                condition    = ${if eq{bad}{${readsocket{/var/run/quotad/mailquotad.sock}\
                                {${lookup mysql{SELECT CONCAT(`username`, ':', `domain`, ':', `quota`)\
                                    FROM TMAILBOX WHERE \
                                        `username`='${quote_mysql:$local_part}' AND \
                                        `domain`='${quote_mysql:$domain}' AND \
                                        `is_active`='Y' }\
                                    }}{2s}{\n}{socket-error}}}{yes}{no}}
```

for using tcp socket change ` readsocket{/var/run/quotad/mailquotad.sock} ` to ` readsocket{inet:10.0.0.10:64003} `