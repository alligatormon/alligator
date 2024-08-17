
https://github.com/yaoweibin/nginx_upstream_check_module
```
aggregate backends {
	nginx_upstream_check http://localhost/uc_status;
}
```

nginx.conf:
```
	location /uc_status {
		check_status;
	}
```
alligator.conf:
```
aggregate {
	nginx_upstream_check http://localhost/uc_status;
}
```
