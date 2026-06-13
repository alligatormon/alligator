#pragma once

#include <stdint.h>

#define DNS_PORT        53

#define DNS_QUERY       0
#define DNS_RESPONSE    1

#define DNS_TYPE_A      1
#define DNS_TYPE_NS     2
#define DNS_TYPE_CNAME  5
#define DNS_TYPE_SOA    6
#define DNS_TYPE_WKS    11
#define DNS_TYPE_PTR    12
#define DNS_TYPE_HINFO  13
#define DNS_TYPE_MX     15
#define DNS_TYPE_TXT    16
#define DNS_TYPE_AAAA   28
#define DNS_TYPE_SRV    33
#define DNS_TYPE_AXFR   252
#define DNS_TYPE_ANY    255

#define DNS_CLASS_IN    1

#define DNS_NAME_MAXLEN 256

#ifndef BYTE_ORDER
#  if defined(__BYTE_ORDER)
#    define BYTE_ORDER __BYTE_ORDER
#  elif defined(__BYTE_ORDER__)
#    define BYTE_ORDER __BYTE_ORDER__
#  elif defined(__DARWIN_BYTE_ORDER)
#    define BYTE_ORDER __DARWIN_BYTE_ORDER
#  elif defined(__LITTLE_ENDIAN__)
#    define BYTE_ORDER __LITTLE_ENDIAN__
#  elif defined(__BIG_ENDIAN__)
#    define BYTE_ORDER __BIG_ENDIAN__
#  elif defined(_WIN32)
#    define BYTE_ORDER LITTLE_ENDIAN
#  else
#    error "BYTE_ORDER undefined!"
#  endif
#endif

#ifndef LITTLE_ENDIAN
#  if defined(__LITTLE_ENDIAN)
#    define LITTLE_ENDIAN __LITTLE_ENDIAN
#  elif defined(__ORDER_LITTLE_ENDIAN__)
#    define LITTLE_ENDIAN __ORDER_LITTLE_ENDIAN__
#  else
#    define LITTLE_ENDIAN 1234
#  endif
#endif

#ifndef BIG_ENDIAN
#  if defined(__BIG_ENDIAN)
#    define BIG_ENDIAN __BIG_ENDIAN
#  elif defined(__ORDER_BIG_ENDIAN__)
#    define BIG_ENDIAN __ORDER_BIG_ENDIAN__
#  else
#    define BIG_ENDIAN 4321
#  endif
#endif

typedef struct dnshdr_s {
	uint16_t    transaction_id;
#if BYTE_ORDER == LITTLE_ENDIAN
	uint8_t     rd:1;
	uint8_t     tc:1;
	uint8_t     aa:1;
	uint8_t     opcode:4;
	uint8_t     qr:1;

	uint8_t     rcode:4;
	uint8_t     cd:1;
	uint8_t     ad:1;
	uint8_t     res:1;
	uint8_t     ra:1;
#elif BYTE_ORDER == BIG_ENDIAN
	uint8_t     qr:1;
	uint8_t     opcode:4;
	uint8_t     aa:1;
	uint8_t     tc:1;
	uint8_t     rd:1;

	uint8_t     ra:1;
	uint8_t     res:1;
	uint8_t     ad:1;
	uint8_t     cd:1;
	uint8_t     rcode:4;
#else
#error "BYTE_ORDER undefined!"
#endif
	uint16_t    nquestion;
	uint16_t    nanswer;
	uint16_t    nauthority;
	uint16_t    naddtional;
} dnshdr_t;

typedef struct dns_rr_s {
	char        name[DNS_NAME_MAXLEN];
	uint16_t    rtype;
	uint16_t    rclass;
	uint32_t    ttl;
	uint16_t    datalen;
	char*       data;
} dns_rr_t;

typedef struct dns_s {
	dnshdr_t    hdr;
	dns_rr_t*   questions;
	dns_rr_t*   answers;
	dns_rr_t*   authorities;
	dns_rr_t*   addtionals;
} dns_t;

int dns_name_encode(const char* domain, char* buf);
int dns_name_decode(const char* buf, char* domain);
int dns_name_decode_msg(const char* buf, const char* msg, char* domain);

int dns_rr_pack(dns_rr_t* rr, char* buf, int len);
int dns_rr_unpack(char* buf, int len, dns_rr_t* rr, int is_question, const char* msg);

int dns_pack(dns_t* dns, char* buf, int len);
int dns_unpack(char* buf, int len, dns_t* dns);
void dns_free(dns_t* dns);
