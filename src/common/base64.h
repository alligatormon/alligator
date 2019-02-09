#pragma once
char *base64_encode(const char *data, size_t input_length, size_t *output_length);
char *base64_decode(const char *data, size_t input_length, size_t *output_length);
//char* b64_decode(const char *in, size_t len, size_t *outlen);
