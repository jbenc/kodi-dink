#include "samtvremotelib.h"

#include <endian.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Reference:
 * http://sc0ty.pl/2012/02/samsung-tv-network-remote-control-protocol/
 */

#define TYPE_KEY		0x0000
#define TYPE_COMMAND		0x0001	/* seen elsewhere, unclear */
#define TYPE_IN_PROGRESS	0x000A
#define TYPE_AUTH		0x0064
#define TYPE_FAILED		0x0065

static const char *app_name;

static int dec_len(unsigned *left, unsigned amount)
{
	if (*left < amount)
		return -1;
	*left -= amount;
	return 0;
}

static int store(void **tail, unsigned *left, const void *data, unsigned len)
{
	if (dec_len(left, len))
		return -1;
	memcpy(*tail, data, len);
	*tail += len;
	return 0;
}

static int skip(void **tail, unsigned *left, unsigned len)
{
	if (dec_len(left, len))
		return -1;
	*tail += len;
	return 0;
}

static int add_u8(void **tail, unsigned *left, uint8_t data)
{
	return store(tail, left, &data, 1);
}

static int add_u16(void **tail, unsigned *left, uint16_t data)
{
	data = htole16(data);
	return store(tail, left, &data, 2);
}

static int add_string(void **tail, unsigned *left, const char *data)
{
	unsigned len = strlen(data);

	if (add_u16(tail, left, len))
		return -1;
	if (store(tail, left, data, len))
		return -1;
	return 0;
}

static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int add_base64(void **tail, unsigned *left, const char *data)
{
	unsigned len = (strlen(data) + 2) / 3 * 4;
	unsigned cnt;
	uint16_t bits;
	uint8_t *dest;

	if (add_u16(tail, left, len))
		return -1;
	if (dec_len(left, len))
		return -1;
	dest = *tail;
	cnt = 0;
	while (*data || cnt) {
		if (cnt < 6) {
			if (*data) {
				bits <<= 8;
				bits |= (uint8_t)*data;
				data++;
				cnt += 8;
			} else {
				bits <<= 6 - cnt;
				cnt = 6;
			}
		}
		*dest++ = base64_chars[(bits >> (cnt - 6)) & 0x3f];
		cnt -= 6;
		len--;
	}
	while (len) {
		*dest++ = '=';
		len--;
	}
	*tail = dest;
	return 0;
}

static int skip_u16(void **tail, unsigned *left)
{
	return skip(tail, left, 2);
}


static int add_header(void **tail, unsigned *left, void **size_entry)
{
	/* datagram type? */
	if (add_u8(tail, left, 0x00))
		return -1;
	if (add_string(tail, left, app_name))
		return -1;
	*size_entry = *tail;
	if (skip_u16(tail, left))
		return -1;
	return 0;
}

static void set_payload_size(void *size_entry, void *tail)
{
	uint16_t data = htole16(tail - size_entry - 2);

	memcpy(size_entry, &data, 2);
}

static int auth_packet(void **tail, unsigned *left, const char *ip_addr, const char *unique_id)
{
	void *size_entry;

	if (add_header(tail, left, &size_entry))
		return -1;
	if (add_u16(tail, left, TYPE_AUTH))
		return -1;
	if (add_base64(tail, left, ip_addr))
		return -1;
	if (add_base64(tail, left, unique_id))
		return -1;
	/* contoller name, can be different than app_name but let's make it
	 * simple */
	if (add_base64(tail, left, app_name))
		return -1;
	set_payload_size(size_entry, *tail);
	return 0;
}

static int key_packet(void **tail, unsigned *left, const char *key)
{
	void *size_entry;

	if (add_header(tail, left, &size_entry))
		return -1;
	if (add_u16(tail, left, TYPE_KEY))
		return -1;
	/* unknown byte */
	if (add_u8(tail, left, 0x00))
		return -1;
	if (add_base64(tail, left, key))
		return -1;
	set_payload_size(size_entry, *tail);
	return 0;
}

void samtv_set_appname(const char *name)
{
	app_name = name;
}

#define alloc_loop(dest, total, tail, left) \
	for (total = 512, dest = malloc(total), tail = dest, left = total;	\
	     dest;								\
	     free(dest), total *= 2)

void *samtv_auth_packet(const char *ip_addr, const char *unique_id, unsigned *len)
{
	void *dest, *tail;
	unsigned total, left;

	alloc_loop(dest, total, tail, left) {
		if (!auth_packet(&tail, &left, ip_addr, unique_id)) {
			*len = total - left;
			return dest;
		}
	}
	return NULL;
}

void *samtv_key_packet(const char *key, unsigned *len)
{
	void *dest, *tail;
	unsigned total, left;

	alloc_loop(dest, total, tail, left) {
		if (!key_packet(&tail, &left, key)) {
			*len = total - left;
			return dest;
		}
	}
	return NULL;
}


static int get(void **tail, unsigned *left, void *data, unsigned len)
{
	if (dec_len(left, len))
		return -1;
	memcpy(data, *tail, len);
	*tail += len;
	return 0;
}

static int get_u8(void **tail, unsigned *left, uint8_t *data)
{
	return get(tail, left, data, 1);
}

static int get_u16(void **tail, unsigned *left, uint16_t *data)
{
	if (get(tail, left, data, 2))
		return -1;
	*data = htole16(*data);
	return 0;
}

static int skip_string(void **tail, unsigned *left)
{
	uint16_t len;

	if (get_u16(tail, left, &len))
		return -1;
	if (dec_len(left, len))
		return -1;
	*tail += len;
	return 0;
}

/* -1: Invalid frame
 *  0: Access denied
 *  1: Access granted
 *  2: In progress
 */
int samtv_check_auth_response(void *data, unsigned len)
{
	uint8_t h_type;
	uint16_t payload_size, type;

	if (get_u8(&data, &len, &h_type))
		return -1;
	if (h_type != 0x00 && h_type != 0x02)
		return -1;
	if (skip_string(&data, &len))
		return -1;
	if (get_u16(&data, &len, &payload_size))
		return -1;
	if (payload_size < len)
		return -1;
	if (get_u16(&data, &len, &type))
		return -1;
	if (type == TYPE_AUTH) {
		if (get_u16(&data, &len, &type))
			return -1;
		return !!type;
	}
	if (type == TYPE_IN_PROGRESS)
		return 2;
	if (type == TYPE_FAILED)
		return 0;
	return -1;
}
