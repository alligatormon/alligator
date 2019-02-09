#include <stdint.h>
#include <stdlib.h>

static char encoding_table[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
				'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
				'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
				'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
				'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
				'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
				'w', 'x', 'y', 'z', '0', '1', '2', '3',
				'4', '5', '6', '7', '8', '9', '+', '/'};

static char decoding_table[] = {62,-1,-1,-1,63,52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-2,-1,-1,-1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,-1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51};
static int mod_table[] = {0, 2, 1};

char *base64_encode(const char *data, size_t input_length, size_t *output_length)
{

	*output_length = 4 * ((input_length + 2) / 3);

	char *encoded_data = malloc(*output_length);
	if (encoded_data == NULL) return NULL;

	int i, j;
	for (i = 0, j = 0; i < input_length;) {

		uint32_t octet_a = i < input_length ? (char)data[i++] : 0;
		uint32_t octet_b = i < input_length ? (char)data[i++] : 0;
		uint32_t octet_c = i < input_length ? (char)data[i++] : 0;

		uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

		encoded_data[j++] = encoding_table[(triple >> 3 * 6) & 0x3F];
		encoded_data[j++] = encoding_table[(triple >> 2 * 6) & 0x3F];
		encoded_data[j++] = encoding_table[(triple >> 1 * 6) & 0x3F];
		encoded_data[j++] = encoding_table[(triple >> 0 * 6) & 0x3F];
	}

	for (i = 0; i < mod_table[input_length % 3]; i++)
		encoded_data[*output_length - 1 - i] = '=';

	return encoded_data;
}

int b64_isvalidchar(char c)
{
	if (c >= '0' && c <= '9')
		return 1;
	else if (c >= 'A' && c <= 'Z')
		return 1;
	else if (c >= 'a' && c <= 'z')
		return 1;
	else if (c == '+' || c == '/' || c == '=')
		return 1;
	return 0;
}

size_t b64_decoded_size(const char *in, size_t len)
{
	size_t ret;
	size_t i;

	if (in == NULL)
		return 0;

	ret = len / 4 * 3;

	for (i=len; i-->0; ) {
		if (in[i] == '=') {
			ret--;
		} else {
			break;
		}
	}

	return ret;
}

char* base64_decode(const char *in, size_t len, size_t *outlen)
{
	size_t i;
	size_t j;
	int    v;

	if (len % 4 != 0) return NULL;
	*outlen = b64_decoded_size(in, len);

	if (!in)
		return 0;

	char *out = malloc(*outlen+1);

	for (i=0; i<len; i++) {
		if (!b64_isvalidchar(in[i])) {
			return 0;
		}
	}

	for (i=0, j=0; i<len; i+=4, j+=3) {
		v = decoding_table[in[i]-43];
		v = (v << 6) | decoding_table[in[i+1]-43];
		v = in[i+2]=='=' ? v << 6 : (v << 6) | decoding_table[in[i+2]-43];
		v = in[i+3]=='=' ? v << 6 : (v << 6) | decoding_table[in[i+3]-43];

		out[j] = (v >> 16) & 0xFF;
		if (in[i+2] != '=')
			out[j+1] = (v >> 8) & 0xFF;
		if (in[i+3] != '=')
			out[j+2] = v & 0xFF;
	}

	out[*outlen] = 0;
	return out;
}
