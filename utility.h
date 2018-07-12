/*
 * Support functions
 */

#ifndef UTILITY_H
#define UTILITY_H

int msleep(int ms);
int is_valid_ip(const char *s);
void send_response(int sock, int ack, const char *str);

#endif

