/*
 * Support functions
 */

#include <time.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


int msleep(int ms)
{
    struct timespec ts;

    if (ms < 1)
        return -1;

    ts.tv_sec = ms / 1000;
    ts.tv_nsec = 1000000 * (ms % 1000);

    return nanosleep(&ts, NULL);
}

int is_valid_ip(const char *s)
{
    struct sockaddr_in sa;

    int result = inet_pton(AF_INET, s, &sa.sin_addr);

    return (result != 0);
}

void send_response(int sock, int ack, const char *str)
{
    int len;

    if (ack)
        len = write(sock, "ack\n", 4);
    else
        len = write(sock, "nak\n", 4);

    if (len < 0) {
        syslog(LOG_WARNING, "send_ack(1): %m\n");
        return;
    }

    if (str && *str) {
        len = strlen(str);

        if (write(sock, str, len) < 0) {
            syslog(LOG_WARNING, "send_ack(2): %m\n");
            return;
        }

        if (str[len - 1] != '\n') {
            if (write(sock, "\n", 1) < 0) {
                syslog(LOG_WARNING, "send_ack(3): %m\n");
                return;
            }
        }
    }
}

