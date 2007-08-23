/*
 * Copyright (c) 2007 Igor Popov <igorpopov@newmail.ru>
 * $Id$
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/signal.h>
#include <signal.h>

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <syslog.h>
#include <err.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>


int
main(int argc, char **argv)
{
    int	ch;
    char *pwname = "mailnull",
                   *grname = "mailnull",
                             *pidfilename = "/var/run/quotad/mailquotad.pid",
                                        *sockname = "/tmp/mailquotad.socket";
    struct passwd  *pw;
    struct group   *gr;
    FILE  *pidfile;
    struct sigaction sigact;
    int	sk_fd = -1, sockopt, fdmax;
    struct sockaddr_un sa;
    fd_set read_fds, master;

    while ((ch = getopt(argc, argv, "p:u:g:s:d")) != -1) {
        switch (ch) {
        case 'p':
            pidfilename = strdup(optarg);
            break;
        case 'u':
            pwname = strdup(optarg);
            break;
        case 'g':
            grname = strdup(optarg);
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
    pw = getpwnam(pwname);
    if (!pw) {
        err(1, "getpwnam('%s') failed", pwname);
    }
    gr = getgrnam(grname);
    if (!gr) {
        err(1, "getgrnam('%s') failed", grname);
    }
    if (setgid(gr->gr_gid) < 0) {
        err(1, "setgid('%d') failed", gr->gr_gid);
    }
    if (setuid(pw->pw_uid) < 0) {
        err(1, "setuid('%d') failed", pw->pw_uid);
    }
    if (daemon(0, 0) < 0) {
        err(1, "daemon() failed");
    }

    if ((pidfile = fopen(pidfilename, "w")) != NULL) {
        fprintf(pidfile, "%d\n", getpid());
        fclose(pidfile);
    }
    openlog("mailquotad", LOG_NDELAY | LOG_PID, LOG_DAEMON);
    syslog(LOG_INFO, "started");

    /* ignore SIGPIPE signal */
    sigact.sa_handler = SIG_IGN;
    sigact.sa_flags = 0;
    if (sigemptyset(&sigact.sa_mask) < 0 ||
            sigaction(SIGPIPE, &sigact, 0) < 0) {
        syslog(LOG_ERR, "failed to ignore SIGPIPE; sigaction: %m");
        goto fail;
    }

    if ((sk_fd = socket(PF_LOCAL, SOCK_STREAM, 0)) < 0) {
        syslog(LOG_ERR, "socket() failed %m");
        goto fail;
    }

    if ((sockopt = fcntl(sk_fd, F_GETFL)) < 0 ||
            fcntl(sk_fd, F_SETFL, sockopt | O_NONBLOCK) < 0) {
        syslog(LOG_ERR, "setting O_NONBLOCK failed: %m");
        goto fail;
    }

    bzero(&sa, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strlcpy(sa.sun_path, sockname, sizeof(sa.sun_path));
    if (bind(sk_fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        syslog(LOG_ERR, "bind() failed: %m");

        if (connect(sk_fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
            syslog(LOG_ERR, "socket %s exists, but not allowing connection, " \
                   "assuming it was left over from forced program termination",
                   sockname);

            if (unlink(sockname) < 0) {
                syslog(LOG_ERR, "Could not unlink existing socket '%s': %m",
                       sockname);
                goto fail;
            }
            if (bind(sk_fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
                syslog(LOG_ERR, "bind() failed: %m");
                goto fail;
            }
            syslog(LOG_DEBUG, "successfully replaced leftover socket '%s'",
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
        syslog(LOG_ERR, "failed chmod on '%s': %m", sockname);
        goto fail;
    }
    if (listen(sk_fd, 150) < 0) {
        syslog(LOG_ERR, "listen() failed: %m");
        goto fail;
    }

    FD_ZERO(&read_fds);
    FD_ZERO(&master);

    /* add the socket to the master set */
    FD_SET(sk_fd, &master);

    fdmax = sk_fd;

    for (;;) {
        int newfd, i, j;

        read_fds = master;

        if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) < 0) {
	    if (errno == EINTR) {
		continue;
	    }
            syslog(LOG_ERR, "select() failed: %m");
            goto fail;
        }

        for (i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &read_fds)) {
                /* we got one */
                if (i == sk_fd) {
		    struct sockaddr_un ra;
                    socklen_t addrlen = sizeof(ra);
                    /* handle new connections */
                    if ((newfd = accept(sk_fd, (struct sockaddr *)&ra, &addrlen)) < 0) {
        		syslog(LOG_ERR, "accept() failed: %m");
                    } else {
                        FD_SET(newfd, &master);
                        /* add to master set */
                        if (newfd > fdmax) {
                            /* keep track of the maximum */
                            fdmax = newfd;
                        }
                    }
                } else {
		    unsigned int nbytes;
		    char buf[1024];

                    /* handle data from a client */
                    if ((nbytes = recv(i, buf, sizeof(buf), 0)) <= 0) {
                        /* got error or connection closed by client */
                        if (nbytes == 0) {
                            /* connection closed */
                        } else {
                            syslog(LOG_ERR, "recv() failed: %m");
                        }
                        close(i);
	                /* bye */
                        FD_CLR(i, &master);
                        /* remove from master set */
                    } else {
                        /* we got some data from a client */
                        for (j = 0; j <= fdmax; j++) {
                            /* send to everyone */
                            if (FD_ISSET(j, &master)) {
                                /* except the listener and ourselves */
                                if (j != sk_fd && j != i) {
				    unsigned int nbytes;
				    char buf[1024];
                                    if (send(j, buf, nbytes, 0) < 0) {
                                        syslog(LOG_ERR, "send() failed: %m");
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }


fail:
    if (sk_fd != -1) {
        close(sk_fd);
    }
    unlink(sockname);
    unlink(pidfilename);
    closelog();

    return EXIT_FAILURE;

    return EXIT_SUCCESS;
}
