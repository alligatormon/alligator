#include <cutter.h>
#include <stdio.h>
#include "common/selector.h"
#include "parsers/prom_format.h"
#include "http.h"
#include "http_reply.h"
#include "base64.h"
#include "selector.h"
#include "validator.h"
#include "url.h"

//void test_labels_parse()
//{
//	puts("=========\ntest label parser\n==========\n");
//	int n=3;
//	int i;
//	char **lblstr;
//	alligator_labels* lbl;
//	lblstr = malloc(sizeof(char)*n);
//	lblstr[0] = strdup("http_requests_total{job=\"apiserver\", handler=\"/api/comments\"} 1");
//	lblstr[1] = strdup("http_requests_total\t{job=\"apiserver\", handler=\"/api/comments\"} 1");
//	lblstr[2] = strdup("http_requests_total\t\n{   job=\"apiserver\", \t    handler =  \"/api/comments\"} 1");
//	for (i=0; i<n; i++)
//	{
//		printf("%d: %s\n", i, lblstr[i]);
//		lbl = labels_parse(lblstr[i], strlen(lblstr[i]));
//		cut_assert_equal_string(lbl->name, "job");
//		cut_assert_equal_string(lbl->key, "apiserver");
//
//		lbl = lbl->next;
//		cut_assert_not_null(lbl);
//		cut_assert_equal_string(lbl->name, "handler");
//		cut_assert_equal_string(lbl->key, "/api/comments");
//		cut_assert_null(lbl->next);
//	}

	//lblstr[0] = strdup("http_requests_total\n{\njob=\"apiserver\", \nhandler=\"/api/comments\",\ndec=\"rcfy\"\n}    \t1");
	//lblstr[1] = strdup("http_requests_total\n{\njob\n=\n\"apiserver\", \nhandler\n=\n\"/api/comments\",\ndec\n=\n\"rcfy\"\n}              1");
	//lblstr[2] = strdup("http_requests_total\n{\njob=\"apiserver\", \nhandler=\"/api/comments\",\ndec=\"rcfy\"\n} \n1");
	//for (i=0; i<n; i++)
	//{
	//	printf("%d: %s\n", i, lblstr[i]);
	//	lbl = labels_parse(lblstr[i], strlen(lblstr[i]));
	//	cut_assert_equal_string(lbl->name, "job");
	//	cut_assert_equal_string(lbl->key, "apiserver");

	//	lbl = lbl->next;
	//	cut_assert_not_null(lbl);
	//	cut_assert_equal_string(lbl->name, "handler");
	//	cut_assert_equal_string(lbl->key, "/api/comments");

	//	lbl = lbl->next;
	//	cut_assert_not_null(lbl);
	//	cut_assert_equal_string(lbl->name, "dec");
	//	cut_assert_equal_string(lbl->key, "rcfy");
	//	cut_assert_null(lbl->next);
	//}

//	lblstr[0] = strdup("http_requests_total\n{\njob=\"apiserver\", \nhandler=\"/api/comments\",\ndec=\"rcfy\", serv=\"serv1\", align=\"c\", cra=\"crcc\", ifname=\"vethfb8375e\", type=\"received_drop\", mountpoint=\"/var/lib/docker/containers/9366f297ae031d03a8db7beee53bb3615b125d341c30017072686417360ac007/shm\", dev=\"sdd\", nc=\"9366f297ae031d03a8db7beee53bb3615b125d341c30017072686417360ac0079366f297ae031d03a8db7beee53bb3615b125d341c30017072686417360ac0079366f297ae031d03a8db7beee53bb3615b125d341c30017072686417360ac0079366f297ae031d03a8db7beee53bb3615b125d341c30017072686417360ac007\"}    \t1");
//	lblstr[1] = strdup("http_requests_total\n{\njob=\"apiserver\", \nhandler=\"/api/comments\",\ndec=\"rcfy\", serv=\"serv1\", align=\"c\", cra=\"crcc\", ifname=\"vethfb8375e\", type=\"received_drop\", mountpoint=\"/var/lib/docker/containers/9366f297ae031d03a8db7beee53bb3615b125d341c30017072686417360ac007/shm\", dev=\"sdd\", nc=\"9366f297ae031d03a8db7beee53bb3615b125d341c30017072686417360ac0079366f297ae031d03a8db7beee53bb3615b125d341c30017072686417360ac0079366f297ae031d03a8db7beee53bb3615b125d341c30017072686417360ac0079366f297ae031d03a8db7beee53bb3615b125d341c30017072686417360ac007\"}\n\t1");
//	lblstr[2] = strdup("http_requests_total{job=\"apiserver\", \nhandler=\"/api/comments\",\ndec=\"rcfy\", serv=\"serv1\", align=\"c\", cra=\"crcc\", ifname=\"vethfb8375e\", type=\"received_drop\", mountpoint=\"/var/lib/docker/containers/9366f297ae031d03a8db7beee53bb3615b125d341c30017072686417360ac007/shm\", dev=\"sdd\", nc=\"9366f297ae031d03a8db7beee53bb3615b125d341c30017072686417360ac0079366f297ae031d03a8db7beee53bb3615b125d341c30017072686417360ac0079366f297ae031d03a8db7beee53bb3615b125d341c30017072686417360ac0079366f297ae031d03a8db7beee53bb3615b125d341c30017072686417360ac007\"\n}\n1");
//	for (i=0; i<n; i++)
//	{
//		printf("%d: %s\n", i, lblstr[i]);
//		lbl = labels_parse(lblstr[i], strlen(lblstr[i]));
//		cut_assert_equal_string(lbl->name, "job");
//		cut_assert_equal_string(lbl->key, "apiserver");
//
//		lbl = lbl->next;
//		cut_assert_not_null(lbl);
//		cut_assert_equal_string(lbl->name, "handler");
//		cut_assert_equal_string(lbl->key, "/api/comments");
//
//		lbl = lbl->next;
//		cut_assert_not_null(lbl);
//		cut_assert_equal_string(lbl->name, "dec");
//		cut_assert_equal_string(lbl->key, "rcfy");
//
//		lbl = lbl->next;
//		cut_assert_not_null(lbl);
//		cut_assert_equal_string(lbl->name, "serv");
//		cut_assert_equal_string(lbl->key, "serv1");
//
//		lbl = lbl->next;
//		cut_assert_not_null(lbl);
//		cut_assert_equal_string(lbl->name, "align");
//		cut_assert_equal_string(lbl->key, "c");
//
//		lbl = lbl->next;
//		cut_assert_not_null(lbl);
//		cut_assert_equal_string(lbl->name, "cra");
//		cut_assert_equal_string(lbl->key, "crcc");
//
//		lbl = lbl->next;
//		cut_assert_not_null(lbl);
//		cut_assert_equal_string(lbl->name, "ifname");
//		cut_assert_equal_string(lbl->key, "vethfb8375e");
//
//		lbl = lbl->next;
//		cut_assert_not_null(lbl);
//		cut_assert_equal_string(lbl->name, "type");
//		cut_assert_equal_string(lbl->key, "received_drop");
//
//		lbl = lbl->next;
//		cut_assert_not_null(lbl);
//		cut_assert_equal_string(lbl->name, "mountpoint");
//		cut_assert_equal_string(lbl->key, "/var/lib/docker/containers/9366f297ae031d03a8db7beee53bb3615b125d341c30017072686417360ac007/shm");
//
//		lbl = lbl->next;
//		cut_assert_not_null(lbl);
//		cut_assert_equal_string(lbl->name, "dev");
//		cut_assert_equal_string(lbl->key, "sdd");
//
//		lbl = lbl->next;
//		cut_assert_not_null(lbl);
//		cut_assert_equal_string(lbl->name, "nc");
//		cut_assert_equal_string(lbl->key, "9366f297ae031d03a8db7beee53bb3615b125d341c30017072686417360ac0079366f297ae031d03a8db7beee53bb3615b125d341c30017072686417360ac0079366f297ae031d03a8db7beee53bb3615b125d341c30017072686417360ac0079366f297ae031d03a8db7beee53bb3615b125d341c30017072686417360ac007");
//
//		cut_assert_null(lbl->next);
//	}
//}
