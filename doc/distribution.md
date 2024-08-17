# Distribution
## Docker
```
docker run -v /app/alligator.conf:/etc/alligator.conf alligatormon/alligator
```

## Centos 7, Centos 9
```
[rpm_alligator]
name=rpm_alligator
baseurl=https://packagecloud.io/amoshi/alligator/el/$releasever/$basearch
repo_gpgcheck=1
gpgcheck=0
enabled=1
gpgkey=https://packagecloud.io/amoshi/alligator/gpgkey
sslverify=1
sslcacert=/etc/pki/tls/certs/ca-bundle.crt
metadata_expire=300

```

## Ubuntu
```
apt install -y curl gnupg apt-transport-https && \
curl -L https://packagecloud.io/amoshi/alligator/gpgkey | apt-key add -
```

### Ubuntu 20.04:
```
echo 'deb https://packagecloud.io/amoshi/alligator/ubuntu/ focal main' | tee /etc/apt/sources.list.d/alligator.list
```

### Ubuntu 22.04:
```
echo 'deb https://packagecloud.io/amoshi/alligator/ubuntu/ jammy main' | tee /etc/apt/sources.list.d/alligator.list
```

### Ubuntu 22.04:
```
echo 'deb https://packagecloud.io/amoshi/alligator/ubuntu/ noble main' | tee /etc/apt/sources.list.d/alligator.list
```

## Debian
### Debian 11
```
echo 'deb https://packagecloud.io/amoshi/alligator/ubuntu bullseye main' | tee /etc/apt/sources.list.d/alligator.list
```

### Debian 12
