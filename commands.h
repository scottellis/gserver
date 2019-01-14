#ifndef COMMANDS_H
#define COMMANDS_H

#define GSERVER_MAJOR_VERSION 0
#define GSERVER_MINOR_VERSION 5

#define CMD_INVALID 0
#define CMD_UNKNOWN 1
#define CMD_VERSION 2
#define CMD_BUILD 3
#define CMD_NETCONFIG 4
#define CMD_DOWNLOAD 5
#define CMD_UPGRADE 6
#define CMD_REBOOT 7
#define CMD_DOWNLOAD_SIG 8

int command_id(const char *str);
void command_version(int sock);
void command_build(int sock);
void command_upgrade(int sock);
void command_reboot(int sock);
void command_netconfig(int sock, const char *args);
void command_download(int sock, int cmd, const char *args);

#endif /* COMMANDS_H */
