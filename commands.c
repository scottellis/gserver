#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <errno.h>
#include <syslog.h>
#include <signal.h>

#include "commands.h"
#include "utility.h"

int save_upload_img(const char *filename, const char *binbuff, int size);

int command_id(const char *str)
{
    int cmd = CMD_INVALID;

    if (!str || *str) {
        if (!strncasecmp(str, "version", 7))
            cmd = CMD_VERSION;
        else if (!strncasecmp(str, "build", 5))
            cmd = CMD_BUILD;
        else if (!strncasecmp(str, "netconfig", 9))
            cmd = CMD_NETCONFIG;
        else if (!strncasecmp(str, "download", 8))
            cmd = CMD_DOWNLOAD;
        else if (!strncasecmp(str, "upgrade", 7))
            cmd = CMD_UPGRADE;
        else if (!strncasecmp(str, "reboot", 6))
            cmd = CMD_REBOOT;
        else
            cmd = CMD_UNKNOWN;
    }

    return cmd;
}

void command_version(int sock)
{
    char buff[32];

    sprintf(buff, "gserver version: %d.%d", GSERVER_MAJOR_VERSION, GSERVER_MINOR_VERSION);

    send_response(sock, 1, buff);
}

void command_build(int sock)
{
    int fd, len;
    char buff[256];

    fd = -1;

    if (access("/etc/gamry_build", R_OK)) {
        syslog(LOG_WARNING, "access: /etc/gamry_build: %m\n");
        send_response(sock, 0, NULL);
        return;
    }

    fd = open("/etc/gamry_build", O_RDONLY);

    if (fd < 0) {
        syslog(LOG_WARNING, "open: /etc/gamry_build: %m\n");
        send_response(sock, 0, NULL);
        return;
    }

    memset(buff, 0, sizeof(buff));

    len = read(fd, buff, sizeof(buff) - 2);

    if (len < 0) {
        syslog(LOG_WARNING, "read: /etc/gamry_build: %m\n");
        close(fd);
        send_response(sock, 0, NULL);
        return;
    }

    close(fd);

    send_response(sock, 1, buff);
}

void command_reboot(int sock)
{
    send_response(sock, 1, "Rebooting now");
    system("/sbin/reboot");
    raise(SIGINT);
}

void command_upgrade(int sock)
{
    send_response(sock, 0, "Upgrade failed");
}


int write_netconfig_interfaces(const char *ip, const char *nm, const char *gw)
{
    char buff[512];

    memset(buff, 0, sizeof(buff));

    strcpy(buff, "# Autogenerated file\n\nauto lo\niface lo inet loopback\n\nauto eth0\n");

    if (!strncasecmp(ip, "dhcp", 4)) {
        strcat(buff, "iface eth0 inet dhcp\n    wait-delay 15\n\n");
    }
    else {
        strcat(buff, "iface eth0 inet static\n    address ");
        strcat(buff, ip);
        strcat(buff, "\n    netmask ");

        if (nm)
            strcat(buff, nm);
        else
            strcat(buff, "255.255.255.0");

        strcat(buff, "\n");

        if (gw) {
            strcat(buff, "    gateway ");
            strcat(buff, gw);
            strcat(buff, "\n");
        }

        strcat(buff, "\n");
    }

    int fd = open("/etc/network/interfaces", O_RDWR | O_CREAT | O_TRUNC, 0644);

    if (fd < 0) {
        syslog(LOG_WARNING, "write_netconfig open() interfaces: %m\n");
        return -1;
    }

    int len = strlen(buff);

    if (write(fd, buff, len) != len) {
        syslog(LOG_WARNING, "write_netconfig write() interfaces: %m\n");
        close(fd);
        return -1;
    }

    close(fd);

    return 0;
}

int write_netconfig_resolv_conf(const char *ns1, const char *ns2)
{
    char buff[128];

    if (!ns1) {
        if (access("/etc/resolv.conf", F_OK)) {
            syslog(LOG_INFO, "access /tmp/resolv_conf: %m\n");
        }
        else {
            if (unlink("/etc/resolv.conf") < 0) {
                syslog(LOG_WARNING, "write_netconfig deleting resolv.conf: %m\n");
                return -1;
            }
        }

        return 0;
    }

    memset(buff, 0, sizeof(buff));

    strcpy(buff, "nameserver ");
    strcat(buff, ns1);
    strcat(buff, "\n");

    if (ns2) {
        strcat(buff, "nameserver ");
        strcat(buff, ns2);
        strcat(buff, "\n");
    }

    int fd = open("/etc/resolv.conf", O_RDWR | O_CREAT | O_TRUNC, 0644);

    if (fd < 0) {
        syslog(LOG_WARNING, "write_netconfig open() resolv.conf: %m\n");
        return -1;
    }

    int len = strlen(buff);

    if (write(fd, buff, len) != len) {
        syslog(LOG_WARNING, "write_netconfig write() resolv.conf: %m\n");
        close(fd);
        return -1;
    }

    close(fd);

    return 0;
}

/*
  Netconfig args is either the string 'dhcp' or a colon delimited string
  in this format

  ip:netmask[:gateway[:nameserver1[:nameserver2]]]

  gateway and the nameservers are optional, but you must have a gateway
  or you can't have a nameserver1, etc...

  Anything else is considered an error.
*/
void command_netconfig(int sock, const char *args)
{
    char *ip = NULL;
    char *nm = NULL;
    char *gw = NULL;
    char *ns1 = NULL;
    char *ns2 = NULL;
    char buff[256];
    char *saveptr;

    if (strlen(args) > 240) {
        send_response(sock, 0, "Invalid ip args");
        return;
    }

    strcpy(buff, args);

    ip = strtok_r(buff, ":", &saveptr);

    if (!ip) {
        send_response(sock, 0, "Invalid netconfig args");
        return;
    }

    if (strncasecmp(ip, "dhcp", 4)) {
        if (!is_valid_ip(ip)) {
            send_response(sock, 0, "Invalid ip");
            return;
        }

        nm = strtok_r(NULL, ":", &saveptr);

        if (nm && !is_valid_ip(nm)) {
            send_response(sock, 0, "Invalid netmask");
            return;
        }

        if (nm) {
            gw = strtok_r(NULL, ":", &saveptr);

            if (gw && !is_valid_ip(gw)) {
                send_response(sock, 0, "Invalid gateway");
                return;
            }

            if (gw) {
                ns1 = strtok_r(NULL, ":", &saveptr);

                if (ns1 && !is_valid_ip(ns1)) {
                    send_response(sock, 0, "Invalid nameserver1");
                    return;
                }

                if (ns1) {
                    ns2 = strtok_r(NULL, ":", &saveptr);

                    if (ns2 && !is_valid_ip(ns2)) {
                        send_response(sock, 0, "Invalid nameserver2");
                        return;
                    }
                }
            }
        }
    }

    if (write_netconfig_interfaces(ip, nm, gw) < 0) {
        send_response(sock, 0, "Error writing netconfig interfaces");
        return;
    }

    if (write_netconfig_resolv_conf(ns1, ns2) < 0) {
        send_response(sock, 0, "Error writing netconfig resolv.conf");
        return;
    }

    send_response(sock, 1, NULL);
}

void command_download(int sock, const char *args)
{
    int pos, len, size, retries;
    char *binbuff = NULL;

    if (!args || !*args) {
        send_response(sock, 0, "NULL size arg for download");
        return;
    }

    size = strtoul(args, NULL, 0);

    if (size < 1 || size > (64 * 1024 * 1024)) {
        send_response(sock, 0, "Invalid size of file download");
        return;
    }

    binbuff = malloc(size);

    if (!binbuff) {
        syslog(LOG_WARNING, "malloc in command download: %m\n");
        send_response(sock, 0, "Failed to allocate memory for download");
        return;
    } 

    pos = 0;
    retries = 0;

    syslog(LOG_INFO, "Starting transfer of %d bytes\n", size);

    while (pos < size && retries < 10) {
        len = read(sock, binbuff + pos, size - pos);

        if (len < 0) {
            syslog(LOG_WARNING, "read sock error in download: %m\n");
            break;
        }

        if (len > 0) {
            pos += len;
            retries = 0;
        }
        else {
            msleep(1000);
            retries++;
        }
    }

    syslog(LOG_INFO, "Transfer complete, saving file to /tmp/upgrade-img.xz\n");

    if (pos < size) {
        free(binbuff);
        send_response(sock, 0, "File transfer timeout");
        return;
    }

    if (save_upload_img("/tmp/upload-img.xz", binbuff, size) < 0)
        send_response(sock, 0, "File save failed"); 
    else
        send_response(sock, 1, "File transfer successful");

    free(binbuff);
}

int save_upload_img(const char *filename, const char *binbuff, int size)
{
    int pos, len;

    int fd = open(filename, O_RDWR | O_TRUNC | O_CREAT, 0644);   

    if (fd < 0) {
        syslog(LOG_WARNING, "Error opening tmp img file: %m\n");
        return -1;
    }

    pos = 0;

    while (pos < size) {
        len = write(fd, binbuff + pos, size - pos);

        if (len < 0) {
            syslog(LOG_WARNING, "Error writing tmp img file: %m\n");
            break;
        }

        pos += len;
    }

    close(fd);

    if (pos < size)
        return -1;

    return 0;    
}



















