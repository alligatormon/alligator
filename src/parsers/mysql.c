#include "uvmyc.h"

#include <stdlib.h>
#include <stdio.h>

#define DBNAME     "performance_schema"
#define DBHOST     "127.0.0.1:3306"
#define DBUSER     "root"
#define DBPASSWORD "qwerty"
void uvmycPulse(uvmyc *conn);

uvmyc *myc_conn;

void onExecute(uvmyc *conn, int status) {
  printf("onExecute status:%d\n", status);
  if (0 == status) {
    printf("uvmycGetFieldCount:%d\n", uvmycGetFieldCount(conn));
    printf("uvmycGetRowCount:%d\n", uvmycGetRowCount(conn));
    printf("uvmycGetRowString:%s\n", uvmycGetRowString(conn, 0, 0));
  }
}

void onIdle(uvmyc *conn, int status) {
  printf("onIdle\n");
  uvmycQueryLimit1000(conn, "SELECT \"232acwvewvfbvwe\"", -1, onExecute);
}

void mysql_init()
{
  return;
  uvmyc *conn = myc_conn = (uvmyc *)malloc(sizeof(uvmyc));
  uvmycInit(conn, 33, DBUSER, DBPASSWORD, DBNAME, DBHOST, uv_default_loop());
  conn->idleCb = onExecute;
  conn->timer.data = conn;
  uvmycPulse(conn);
}

void mysql_run()
{
  return;
  //uvmycQueryLimit1000(conn, "SELECT \"232acwvewvfbvwe\"", -1, onExecute);
  uvmycExecute(myc_conn, "SELECT \"232acwvewvfbvwe\"", -1, onExecute);
  //uvmycStart(conn, onIdle);
}

