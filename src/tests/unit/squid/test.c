#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void squid_pconn_handler(char *metrics, size_t read_size, void *data)
{
	uint64_t pos = 0;
	uint64_t requests_pos = 0;
	char pool[255];
	uint64_t copy_size;

	uint64_t hash_table_offset = 0;

	for (pos = 0; pos < read_size; pos++)
	{
		tmp = strstr(metrics+pos, "by")
		if (!tmp)
			break;

		tmp += 2;
		tmp += strspn(tmp, " \t\r\n");
		copy_size = strcspn(tmp, " \t\r\n");
		strlcpy(pool, tmp, copy_size);

		printf("kid: %s\n", pool)

		tmp = strstr(tmp, "Connection Count");
		if (!tmp)
			break;

		tmp += 16;
		tmp += strspn(tmp, " -\t\r\n");

		hash_table_offset = (strstr(tmp, "Hash Table") - tmp);
		for (requests_pos = 0; requests_pos < hash_table_offset; requests_pos++)
		{
			
		}
	}
}

int main()
{
	puts("squid_test");
	FILE *fd;

	size_t tmp_size = 1024*1024;
	char *tmp = malloc(tmp_size);
	
	fd = fopen("tests/unit/squid/pconn", "r");
	perror("fopen:");
	printf("fd %p\n", fd);
	if (fd)
	{
		memset(tmp, 0, tmp_size);
		size_t read_size = fread(tmp, tmp_size, 1, fd);
		fclose(fd);

		squid_pconn_handler(tmp, read_size, NULL);
	}
}


by kid1 {

 Pool 0 Stats
server-peers persistent connection counts:

	 Requests	 Connection Count
	 --------	 ----------------
	0	1
	1	383
	2	3
	3	9
	4	4
	5	1
	6	1
	7	1
	8	1
	13	2
	14	1
	18	1
	20	1
	22	1
	25	1
	27	1
	37	1
	48	1
	53	1

 Pool 0 Hash Table
	 item 0:	185.5.72.155:80/st.fotocdn.net
	 item 1:	46.254.18.41:80/konkovomedia.ru
	 item 2:	12.123.67.117:80/daily.example.com
	 item 3:	12.123.67.116:80/editor.example.com
	 item 4:	185.77.233.101:80/www.nim.ru
	 item 5:	98.58.12.18:443/corr.example.com
	 item 6:	46.254.18.41:80/teplyystanmedia.ru
	 item 7:	12.123.67.113:80/example.ru
	 item 8:	194.85.1.18:80/minpromtorg.gov.ru
	 item 9:	46.254.18.41:80/severnoebutovomedia.ru
	 item 10:	195.19.220.16:80/rss66.ngs23.ru
	 item 11:	204.11.56.48:80/sexgirlsnude.com
	 item 12:	213.174.135.2:80/s9442214.pix-cdn.org
	 item 13:	157.240.205.1:443/graph.facebook.com
	 item 14:	12.123.67.116:80/s1.example.com
	 item 15:	12.123.67.117:443/daily.example.com
	 item 16:	37.143.8.45:80/sovet.babush.ru
	 item 17:	185.159.81.170:80/www.tadviser.ru
	 item 18:	31.192.109.57:80/trip-point.ru
	 item 19:	12.123.67.116:443/s1.example.com
	 item 20:	46.254.18.41:80/gagarinskiymedia.ru
	 item 21:	46.254.18.41:80/akademicheskiymedia.ru
	 item 22:	5.23.53.131:80/prim.news
	 item 23:	217.12.104.100:80/alfabank.ru
	 item 24:	12.123.67.86:443/www.example.com
	 item 25:	12.123.67.67:80/bookie2.championat.com
	 item 26:	46.254.18.41:80/zyuzinomedia.ru
	 item 27:	98.58.12.18:80/corr.example.com
	 item 28:	188.225.77.15:80/www.vanlife.ru
	 item 29:	188.124.32.70:80/www.ok-magazine.ru
	 item 30:	148.251.138.156:80/www.livecars.ru
	 item 31:	94.127.68.41:80/permnew.ru
	 item 32:	98.51.87.28:80/api.example.com
	 item 33:	12.123.67.86:80/www.example.com
	 item 34:	93.158.134.60:80/im-tub.yandex.ru
	 item 35:	12.123.67.80:80/img.example.com
	 item 36:	195.208.1.119:80/miamusic.ru
	 item 37:	62.76.89.135:80/www.sobaka.ru
	 item 38:	46.254.18.41:80/yasenevomedia.ru
	 item 39:	12.123.67.117:80/gorod.example.com
	 item 40:	217.65.2.224:80/www.4living.ru
	 item 41:	12.123.67.116:443/editor.example.com
	 item 42:	91.226.172.10:80/voronovskoe.ru
	 item 43:	87.236.16.195:80/kino-punk.ru
	 item 44:	104.19.217.61:443/s-img.lentainform.com
	 item 45:	90.156.140.19:80/www.fashiontime.ru
} by kid1
