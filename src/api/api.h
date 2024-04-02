#pragma once
#include "common/selector.h"
#include "parsers/http_proto.h"

void http_api_v1(string *response, http_reply_data* http_data, const char *configbody);
