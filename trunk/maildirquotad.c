/*
 *  Copyright (c) 2007 Igor Popov <igorpopov@newmail.ru>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.

 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 * $Id$
 *
 */

#include <sys/queue.h>
#include <sys/types.h>
#include <sys/param.h>

#include <sys/time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>

#include <unistd.h>
#include <libutil.h>
#include <fcntl.h>
#include <err.h>
#include <errno.h>
#include <pwd.h>
#include <syslog.h>
#include <fts.h>
#include <limits.h>

#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <event.h>


extern char *__progname;

static unsigned int client_count = 0, id_count = 0;

typedef struct client {
    u_int32_t                id;
//    struct sockaddr_storage  client_ss;
    struct bufferevent      *client_bufev;
    int                      client_fd;
    char                     cbuf[PATH_MAX];
    size_t                   cbuf_valid;
    LIST_ENTRY(client)	     entry;
} client_t, *p_client_t;

LIST_HEAD(, client) clients = LIST_HEAD_INITIALIZER(clients);


static p_client_t
init_client(void)
{
    p_client_t p;

    p = calloc(1, sizeof(client_t));
    if (p == NULL) {
        syslog(LOG_CRIT, "%s calloc failed: %m", __FUNCTION__);
        return (NULL);
    }
    p->id = id_count++;
    p->client_fd = -1;
    p->cbuf[0] = '\0';
    p->cbuf_valid = 0;
    p->client_bufev = NULL;

    LIST_INSERT_HEAD(&clients, p, entry);
    client_count++;

    return (p);
}

static void
end_client(p_client_t p)
{
    syslog(LOG_INFO, "#%d ending client", p->id);

    if (p->client_fd != -1)
        close(p->client_fd);

    if (p->client_bufev)
        bufferevent_free(p->client_bufev);

    LIST_REMOVE(p, entry);
    free(p);
    client_count--;
}

static void
client_error(struct bufferevent *bufev, short what, void *arg)
{
    p_client_t p = arg;

    if (what & EVBUFFER_EOF)
        syslog(LOG_INFO, "#%d client close", p->id);
    else if (what == (EVBUFFER_ERROR | EVBUFFER_READ))
        syslog(LOG_ERR, "#%d client reset connection", p->id);
    else if (what & EVBUFFER_TIMEOUT)
        syslog(LOG_ERR, "#%d client timeout", p->id);
    else if (what & EVBUFFER_WRITE)
        syslog(LOG_ERR, "#%d client write error: %d", p->id, what);
    else
        syslog(LOG_ERR, "#%d abnormal client error: %d", p->id, what);

    end_client(p);
}

static unsigned int
getline(char *buf, size_t *valid, char *linebuf, size_t *p_linelen)
{
    size_t i;

    if (*valid > PATH_MAX)
        return (-1);

    /* Copy to linebuf while searching for a newline. */
    for (i = 0; i < *valid; i++) {
        linebuf[i] = buf[i];
        if (buf[i] == '\0' || buf[i] == '\n')
            break;
    }

    if (i == *valid) {
        /* No newline found. */
        linebuf[0] = '\0';
        *p_linelen = 0;
        if (i < PATH_MAX)
            return (0);
        return (-1);
    }

    linebuf[i] = '\0';
    *p_linelen = i + 1;
    *valid -= *p_linelen;

    /* Move leftovers to the start. */
    if (*valid != 0)
        memmove(buf, buf + *p_linelen, *valid);

    return *p_linelen;
}

static void
client_read(struct bufferevent *bufev, void *arg)
{
    p_client_t  p = arg;
    size_t buf_avail, read;
    char linebuf[PATH_MAX + 1];
    size_t linelen;
    long long unsigned int savednumber = 0;
    int n;

    do {
        buf_avail = sizeof(p->cbuf) - p->cbuf_valid;
        read = bufferevent_read(bufev, p->cbuf + p->cbuf_valid,
                                buf_avail);
        p->cbuf_valid += read;

        while ((n = getline(p->cbuf, &p->cbuf_valid, linebuf, &linelen)) > 0) {
            struct stat sb;

            syslog(LOG_DEBUG, "#%d client: '%s'", p->id, linebuf);

            if (stat(linebuf, &sb) < 0) {
                syslog(LOG_ERR, "#%d client stat() on '%s' failed: %m", p->id, linebuf);
                continue;
            }

            if (S_ISREG(sb.st_mode)) {
                savednumber = sb.st_size;
            } else if (S_ISDIR(sb.st_mode)) {
                char * const path[] = { linebuf, NULL };
                FTSENT *cur;
                FTS *ftsp;

                ftsp = fts_open(path, FTS_LOGICAL | FTS_COMFOLLOW | FTS_NOCHDIR, NULL);
                if (!ftsp) {
                    syslog(LOG_CRIT, "#%d client fts_open('%s') failed: %m", p->id, linebuf);
                    continue;
                }
                while ((cur = fts_read(ftsp))) {
                    switch (cur->fts_info) {
                    case FTS_DP:
                        /* we only visit in preorder */
                        continue;
                    case FTS_F:
                    case FTS_DEFAULT:
			savednumber += cur->fts_statp->st_size;
                    case FTS_D:
                    case FTS_DNR:
                    case FTS_NS:
                    case FTS_NSOK:
                    case FTS_SLNONE:
                    case FTS_SL:
                        break;
                    case FTS_DC: /* FALLTHROUGH */
                    default:
                        goto nax;
                    }
                }
nax:
                if (fts_close(ftsp)) {
                    syslog(LOG_CRIT, "#%d client fts_close() failed: %m", p->id);
                }
            } else {
                syslog(LOG_ERR, "#%d client '%s' is not regular file and is not directory", p->id, linebuf);
                continue;
            }

            syslog(LOG_DEBUG, "#%d client Mailbox is %llu bytes", p->id, savednumber);
            linelen = snprintf(linebuf, sizeof(linebuf), "%llu", savednumber);
            linebuf[sizeof(linebuf)-1] = '\0';
            if (send(p->client_fd, linebuf, linelen, 0) < 0) {
                syslog(LOG_ERR, "#%d client send() failed: %m", p->id);
            }
        }

        if (n == -1) {
            syslog(LOG_ERR, "#%d client command too long or not clean", p->id);
            end_client(p);
            return;
        }
    } while (read == buf_avail);
}


static void
handle_connection(const int fd, short event, void *arg)
{
    int sk_fd;
    p_client_t p;
    struct sockaddr_un addr;
    socklen_t addrlen = sizeof(addr);

    if ((sk_fd = accept(fd, (struct sockaddr*)&addr, &addrlen)) < 0) {
        syslog(LOG_ERR, "accept() failed: %m");
        return;
    }

    /* Allocate client and copy back the info from the accept(). */
    p = init_client();
    p->client_fd = sk_fd;

    /*
     * Setup buffered events.
     */
    p->client_bufev = bufferevent_new(p->client_fd, &client_read, NULL,
                                      &client_error, p);
    if (p->client_bufev == NULL) {
        syslog(LOG_CRIT, "#%d bufferevent_new client failed", p->id);
        end_client(p);
    }
    bufferevent_settimeout(p->client_bufev, 120, 0);
    bufferevent_enable(p->client_bufev, EV_READ | EV_TIMEOUT);

    return;
}

int
main(int argc, char **argv)
{
    int	ch;
    char *pwname = "mailnull",
                   *pidfilename = "/var/run/mailquotad.pid",
                                  *sockname = "/tmp/mailquotad.socket";
    struct passwd *pw;
    pid_t pid;
    struct pidfh *pfh;
    int	sk_fd = -1, sockopt;
    struct sockaddr_un sun;
    struct event ev;

    while ((ch = getopt(argc, argv, "p:u:s:d")) != -1) {
        switch (ch) {
        case 'p':
            pidfilename = strdup(optarg);
            break;
        case 'u':
            pwname = strdup(optarg);
            break;
        case 's':
            sockname = strdup(optarg);
            break;
        default:
            err(1, "Invalid option specified, terminating");
        }
    }
    argc -= optind;
    argv += optind;

    if (argc > 0) {
        err(1, "bogus extra arguments");
    }

    if (getuid() != 0)
        errx(1, "needs to start as root");

    if ((pw = getpwnam(pwname)) == NULL) {
        err(1, "getpwnam('%s') failed: %m", pwname);
    }

    pfh = pidfile_open(pidfilename, 0600, &pid);
    if (pfh == NULL) {
        if (errno == EEXIST) {
            errx(EXIT_FAILURE, "Daemon already running, pid: %jd.",
                 (intmax_t)pid);
        }
        /* If we cannot create pidfile from other reasons, only warn. */
        warn("Cannot open or create pidfile");
    }

    if (setgroups(1, &pw->pw_gid) < 0 ||
            setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) < 0 ||
            setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid) < 0)
    {
        err(1, "set gid to %d and uid to %d failed: %m", pw->pw_gid, pw->pw_uid);
    }

    if (daemon(0, 0) < 0) {
        err(1, "daemon() failed");
    }

    pidfile_write(pfh);

    openlog(__progname, LOG_NDELAY | LOG_PID, LOG_DAEMON);
    syslog(LOG_INFO, "started");

    /* ignore SIGPIPE signal */
    signal(SIGPIPE, SIG_IGN);

    if ((sk_fd = socket(PF_LOCAL, SOCK_STREAM, 0)) < 0) {
        syslog(LOG_ERR, "socket() failed %m");
        goto fail;
    }

    if ((sockopt = fcntl(sk_fd, F_GETFL)) < 0 ||
            fcntl(sk_fd, F_SETFL, sockopt | O_NONBLOCK) < 0) {
        syslog(LOG_ERR, "setting O_NONBLOCK failed: %m");
        goto fail;
    }

    bzero(&sun, sizeof(sun));
    sun.sun_family = AF_UNIX;
    strlcpy(sun.sun_path, sockname, sizeof(sun.sun_path));
    if (bind(sk_fd, (struct sockaddr *)&sun, sizeof(sun)) < 0) {
        syslog(LOG_ERR, "bind() failed: %m");

        if (connect(sk_fd, (struct sockaddr *)&sun, sizeof(sun)) < 0) {
            syslog(LOG_ERR, "socket %s exists, but not allowing connection, " \
                   "assuming it was left over from forced program termination",
                   sockname);

            if (unlink(sockname) < 0) {
                syslog(LOG_ERR, "Could not unlink existing socket '%s': %m",
                       sockname);
                goto fail;
            }
            if (bind(sk_fd, (struct sockaddr *)&sun, sizeof(sun)) < 0) {
                syslog(LOG_ERR, "bind() failed: %m");
                goto fail;
            }
            syslog(LOG_NOTICE, "successfully replaced leftover socket '%s'",
                   sockname);
        } else {
            syslog(LOG_ERR, "socket %s exists and seems to " \
                   "be in use, cannot override it " \
                   "delete it by hands if it is not used anymore",
                   sockname);
            goto fail;
        }
    }
    if (chmod(sockname, S_IRWXU | S_IRWXG) < 0) {
        syslog(LOG_ERR, "chmod on '%s' failed: %m", sockname);
        goto fail;
    }
    if (listen(sk_fd, 150) < 0) {
        syslog(LOG_ERR, "listen() failed: %m");
        goto fail;
    }

    /* Initalize the event library */
    event_init();

    event_set(&ev, sk_fd, EV_READ | EV_PERSIST, handle_connection, &ev);

    /* Add it to the active events, without a timeout */
    event_add(&ev, NULL);

    event_dispatch();

fail:
    if (sk_fd != -1) {
        close(sk_fd);
    }

    unlink(sockname);

    if (pfh)
        pidfile_remove(pfh);
    closelog();

    return EXIT_FAILURE;
}
