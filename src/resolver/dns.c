#include "resolver/dns.h"

#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

void dns_free(dns_t* dns)
{
	free(dns->questions);
	free(dns->answers);
	free(dns->authorities);
	free(dns->addtionals);
	dns->questions = NULL;
	dns->answers = NULL;
	dns->authorities = NULL;
	dns->addtionals = NULL;
}

int dns_name_encode(const char* domain, char* buf)
{
	const char* p = domain;
	char* plen = buf++;
	int buflen = 1;
	int len = 0;

	while (*p != '\0') {
		if (*p != '.') {
			++len;
			*buf = *p;
		} else {
			*plen = len;
			plen = buf;
			len = 0;
		}
		++p;
		++buf;
		++buflen;
	}
	*plen = len;
	*buf = '\0';
	if (len != 0)
		++buflen;
	return buflen;
}

int dns_name_decode(const char* buf, char* domain)
{
	return dns_name_decode_msg(buf, buf, domain);
}

int dns_name_decode_msg(const char* buf, const char* msg, char* domain)
{
	const char* p = buf;
	const char* domain_start = domain;
	int consumed = 0;
	int jumped = 0;
	int steps = 0;

	while (1) {
		if (++steps > 128)
			return -1;

		if (((uint8_t)*p & 0xc0) == 0xc0) {
			uint16_t offset = ((uint16_t)((uint8_t)*p & 0x3f) << 8) | (uint8_t)*(p + 1);
			if (!msg || (!jumped && offset >= (size_t)(buf - msg)))
				return -1;
			if (!jumped) {
				consumed += 2;
				jumped = 1;
			}
			p = msg + offset;
			continue;
		}

		int len = (uint8_t)*p++;
		if (!jumped)
			++consumed;

		if (len == 0) {
			if (domain != domain_start)
				--domain;
			*domain = '\0';
			return consumed;
		}

		if (len > 63)
			return -1;

		while (len-- > 0) {
			*domain++ = *p++;
			if (!jumped)
				++consumed;
		}
		*domain++ = '.';
	}
}

int dns_rr_pack(dns_rr_t* rr, char* buf, int len)
{
	char* p = buf;
	char encoded_name[256];
	int encoded_namelen = dns_name_encode(rr->name, encoded_name);
	int packetlen = encoded_namelen + 2 + 2 + (rr->data ? (4 + 2 + rr->datalen) : 0);

	if (len < packetlen)
		return -1;

	memcpy(p, encoded_name, encoded_namelen);
	p += encoded_namelen;
	*(uint16_t*)p = htons(rr->rtype);
	p += 2;
	*(uint16_t*)p = htons(rr->rclass);
	p += 2;

	if (rr->datalen && rr->data) {
		*(uint32_t*)p = htonl(rr->ttl);
		p += 4;
		*(uint16_t*)p = htons(rr->datalen);
		p += 2;
		memcpy(p, rr->data, rr->datalen);
		p += rr->datalen;
	}
	return (int)(p - buf);
}

int dns_rr_unpack(char* buf, int len, dns_rr_t* rr, int is_question, const char* msg)
{
	char* p = buf;
	int off = 0;
	int namelen;

	memset(rr, 0, sizeof(*rr));
	if (*(uint8_t*)p >= 192) {
		namelen = dns_name_decode_msg(p, msg, rr->name);
	} else {
		namelen = dns_name_decode_msg(p, msg, rr->name);
	}
	if (namelen < 0)
		return -1;
	p += namelen;
	off += namelen;

	if (len < off + 4)
		return -1;
	rr->rtype = ntohs(*(uint16_t*)p);
	p += 2;
	rr->rclass = ntohs(*(uint16_t*)p);
	p += 2;
	off += 4;

	if (!is_question) {
		if (len < off + 6)
			return -1;
		rr->ttl = ntohl(*(uint32_t*)p);
		p += 4;
		rr->datalen = ntohs(*(uint16_t*)p);
		p += 2;
		off += 6;
		if (len < off + rr->datalen)
			return -1;
		rr->data = p;
		p += rr->datalen;
		off += rr->datalen;
	}
	return off;
}

int dns_pack(dns_t* dns, char* buf, int len)
{
	if (len < (int)sizeof(dnshdr_t))
		return -1;

	int off = 0;
	dnshdr_t htonhdr = dns->hdr;
	htonhdr.transaction_id = htons(dns->hdr.transaction_id);
	htonhdr.nquestion = htons(dns->hdr.nquestion);
	htonhdr.nanswer = htons(dns->hdr.nanswer);
	htonhdr.nauthority = htons(dns->hdr.nauthority);
	htonhdr.naddtional = htons(dns->hdr.naddtional);
	memcpy(buf, &htonhdr, sizeof(dnshdr_t));
	off += sizeof(dnshdr_t);

	for (int i = 0; i < dns->hdr.nquestion; ++i) {
		int packetlen = dns_rr_pack(dns->questions + i, buf + off, len - off);
		if (packetlen < 0)
			return -1;
		off += packetlen;
	}
	for (int i = 0; i < dns->hdr.nanswer; ++i) {
		int packetlen = dns_rr_pack(dns->answers + i, buf + off, len - off);
		if (packetlen < 0)
			return -1;
		off += packetlen;
	}
	for (int i = 0; i < dns->hdr.nauthority; ++i) {
		int packetlen = dns_rr_pack(dns->authorities + i, buf + off, len - off);
		if (packetlen < 0)
			return -1;
		off += packetlen;
	}
	for (int i = 0; i < dns->hdr.naddtional; ++i) {
		int packetlen = dns_rr_pack(dns->addtionals + i, buf + off, len - off);
		if (packetlen < 0)
			return -1;
		off += packetlen;
	}
	return off;
}

int dns_unpack(char* buf, int len, dns_t* dns)
{
	memset(dns, 0, sizeof(dns_t));
	if (len < (int)sizeof(dnshdr_t))
		return -1;

	int off = 0;
	dnshdr_t* hdr = &dns->hdr;
	memcpy(hdr, buf, sizeof(dnshdr_t));
	off += sizeof(dnshdr_t);
	hdr->transaction_id = ntohs(hdr->transaction_id);
	hdr->nquestion = ntohs(hdr->nquestion);
	hdr->nanswer = ntohs(hdr->nanswer);
	hdr->nauthority = ntohs(hdr->nauthority);
	hdr->naddtional = ntohs(hdr->naddtional);

	const char* msg = buf;

	if (hdr->nquestion) {
		dns->questions = calloc(hdr->nquestion, sizeof(dns_rr_t));
		if (!dns->questions)
			return -1;
		for (int i = 0; i < hdr->nquestion; ++i) {
			int packetlen = dns_rr_unpack(buf + off, len - off, dns->questions + i, 1, msg);
			if (packetlen < 0) {
				dns_free(dns);
				return -1;
			}
			off += packetlen;
		}
	}
	if (hdr->nanswer) {
		dns->answers = calloc(hdr->nanswer, sizeof(dns_rr_t));
		if (!dns->answers) {
			dns_free(dns);
			return -1;
		}
		for (int i = 0; i < hdr->nanswer; ++i) {
			int packetlen = dns_rr_unpack(buf + off, len - off, dns->answers + i, 0, msg);
			if (packetlen < 0) {
				dns_free(dns);
				return -1;
			}
			off += packetlen;
		}
	}
	if (hdr->nauthority) {
		dns->authorities = calloc(hdr->nauthority, sizeof(dns_rr_t));
		if (!dns->authorities) {
			dns_free(dns);
			return -1;
		}
		for (int i = 0; i < hdr->nauthority; ++i) {
			int packetlen = dns_rr_unpack(buf + off, len - off, dns->authorities + i, 0, msg);
			if (packetlen < 0) {
				dns_free(dns);
				return -1;
			}
			off += packetlen;
		}
	}
	if (hdr->naddtional) {
		dns->addtionals = calloc(hdr->naddtional, sizeof(dns_rr_t));
		if (!dns->addtionals) {
			dns_free(dns);
			return -1;
		}
		for (int i = 0; i < hdr->naddtional; ++i) {
			int packetlen = dns_rr_unpack(buf + off, len - off, dns->addtionals + i, 0, msg);
			if (packetlen < 0) {
				dns_free(dns);
				return -1;
			}
			off += packetlen;
		}
	}
	return off;
}
