#ifndef _SAMTVREMOTELIB_H
#define _SAMTVREMOTELIB_H

void samtv_set_appname(const char *name);
void *samtv_auth_packet(const char *ip_addr, const char *unique_id, unsigned *len);
void *samtv_key_packet(const char *key, unsigned *len);
int samtv_check_auth_response(void *data, unsigned len);

#endif
