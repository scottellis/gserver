/*
 * Core dispatch loop for Gamry gserver
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <errno.h>
#include <syslog.h>
#include <signal.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/select.h>

#include "commands.h"
#include "utility.h"

#define DEFAULT_DISPATCH_LISTENER_PORT 1234

volatile sig_atomic_t shutdown_signal = 0;

void sig_handler(int sig)
{
    if (sig == SIGINT || sig == SIGTERM || sig == SIGHUP)
        shutdown_signal = 1;
}

int add_signal_handlers()
{
    struct sigaction sia;

    memset(&sia, 0, sizeof(sia));

    sia.sa_handler = sig_handler;

    if (sigaction(SIGINT, &sia, NULL) < 0) {
        syslog(LOG_ERR, "sigaction(SIGINT): %m");
        return -1;
    }
    else if (sigaction(SIGTERM, &sia, NULL) < 0) {
        syslog(LOG_ERR, "sigaction(SIGTERM): %m");
        return -1;
    }
    else if (sigaction(SIGHUP, &sia, NULL) < 0) {
        syslog(LOG_ERR, "sigaction(SIGHUP): %m");
        return -1;
    }

    return 0;
}

int start_dispatch_listener(int port)
{
    int s, on;
    struct sockaddr_in addr;

    if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        syslog(LOG_ERR, "socket: %m");
        return -1;
    }

    on = 1;

    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
        syslog(LOG_ERR, "setsockopt: %m");
        close(s);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        syslog(LOG_ERR, "bind: %m");
        close(s);
        return -1;
    }

    if (listen(s, 1) < 0) {
        syslog(LOG_ERR, "listen: %m");
        close(s);
        return -1;
    }

    syslog(LOG_INFO, "listening on port %d\n", port);

    return s;
}

int read_line(int sock, char *buff, int maxlen)
{
    int ret = 0;
    int pos = 0;
    int inactivity_timeout = 0;

    while (!shutdown_signal) {
        // a byte at a time
        int len = read(sock, buff + pos, 1);

        if (len < 0) {
            // client closed socket
            syslog(LOG_WARNING, "client read error: %m");
            ret = -1;
            break;
        }
        else if (len == 0) {
            ret = msleep(50);

            if (ret < 0) {
                syslog(LOG_WARNING, "nanosleep in read_next_cmd: %m");
                break;
            }

            inactivity_timeout += 50;

            if (inactivity_timeout > 5000) {
                // force client to do something or leave since
                // we only service one client at a time
                syslog(LOG_INFO, "client read timed out\n");
                ret = -1;
                break;
            }
        }
        else {
            inactivity_timeout = 0;

            if (buff[pos] == '\n') {
                buff[pos] = 0;
                break;
            }

            if (pos++ >= (maxlen - 1)) {
                ret = -1;
                break;
            }
        }
    }

    return ret;
}

#define MAX_COMMAND_ARG 120
void handle_client(int sock)
{
    int ret;
    char rx[MAX_COMMAND_ARG + 8];
    char args[MAX_COMMAND_ARG + 8];

    memset(rx, 0, sizeof(rx));

    ret = read_line(sock, rx, MAX_COMMAND_ARG);

    if (ret < 0)
        return;

    int cmd = command_id(rx);

    switch(cmd) {
    case CMD_UNKNOWN:
        send_response(sock, 0, "Unknown command");
        break;

    case CMD_VERSION:
         command_version(sock);
        break;

    case CMD_BUILD:
        command_build(sock);
        break;

    case CMD_NETCONFIG:
        memset(args, 0, sizeof(args));

        ret = read_line(sock, args, MAX_COMMAND_ARG);

        if (ret < 0)
            send_response(sock, 0, "Error reading netconfig args");
        else
            command_netconfig(sock, args);

        break;

    case CMD_DOWNLOAD:
        memset(args, 0, sizeof(args));

        ret = read_line(sock, args, MAX_COMMAND_ARG);

        if (ret < 0)
            send_response(sock, 0, "Error reading download size");
        else
            command_download(sock, args);

        break;

    case CMD_UPGRADE:
        command_upgrade(sock);
        break;

    case CMD_REBOOT:
        command_reboot(sock);
        break;

    case CMD_INVALID:
    default:
        send_response(sock, 0, "Invalid command");
        break;
    }
}

void dispatch_loop(int dispatch_socket)
{
    struct sockaddr_in c_addr_in;
    socklen_t c_len;
    int c_sock;
    fd_set rset;
    struct timespec timeout;
    char ip[INET_ADDRSTRLEN + 8];
    int c_count = 0;

    syslog(LOG_NOTICE, "starting dispatch_loop");

    timeout.tv_sec = 15;
    timeout.tv_nsec = 0;

    while (!shutdown_signal) {
        FD_ZERO(&rset);
        FD_SET(dispatch_socket, &rset);

        if (pselect(dispatch_socket + 1, &rset, NULL, NULL, &timeout, NULL) < 0) {
            if (errno != EINTR)
                syslog(LOG_WARNING, "pselect: %m");
        }
        else if (FD_ISSET(dispatch_socket, &rset)) {
            c_len = sizeof(c_addr_in);

            c_sock = accept(dispatch_socket, (struct sockaddr *) &c_addr_in, &c_len);

            if (c_sock < 0) {
                syslog(LOG_WARNING, "accept: %m");
            }
            else {
                memset(ip, 0, sizeof(ip));
                inet_ntop(AF_INET, &c_addr_in.sin_addr, ip, INET_ADDRSTRLEN);
                syslog(LOG_INFO, "new client %d %s", ++c_count, ip);
                handle_client(c_sock);
                close(c_sock);
                syslog(LOG_INFO, "closed client socket\n");
            }
        }
        else {
            // syslog(LOG_INFO, "dispatch_loop timeout");
            // background processing could be done here
        }
    }
}

int main(int argc, char ** argv)
{
    int dispatch_socket;

    if (daemon(0, 0)) {
        perror("daemon");
        exit(1);
    }

    openlog(argv[0], LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "Version %d.%d", GSERVER_MAJOR_VERSION, GSERVER_MINOR_VERSION);

    if (add_signal_handlers() < 0)
        exit(1);

    dispatch_socket = start_dispatch_listener(DEFAULT_DISPATCH_LISTENER_PORT);

    if (dispatch_socket < 0)
        exit(1);

    dispatch_loop(dispatch_socket);

    close(dispatch_socket);

    syslog(LOG_NOTICE, "shutting down");

    closelog();

    return 0;
}

