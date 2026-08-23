**Language / Язык:** [English](../distribution.md) | [Русский](distribution.md)

# Дистрибуция

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
Установите зависимости и импортируйте GPG-ключ Packagecloud. На Debian/Ubuntu 22.04+ предпочтительнее `signed-by` вместо устаревшего `apt-key`:

```
apt install -y curl gnupg apt-transport-https
curl -fsSL https://packagecloud.io/amoshi/alligator/gpgkey | gpg --dearmor -o /usr/share/keyrings/alligator-packagecloud.gpg
```

### Ubuntu 20.04:
```
echo 'deb [signed-by=/usr/share/keyrings/alligator-packagecloud.gpg] https://packagecloud.io/amoshi/alligator/ubuntu/ focal main' | tee /etc/apt/sources.list.d/alligator.list
```

### Ubuntu 22.04:
```
echo 'deb [signed-by=/usr/share/keyrings/alligator-packagecloud.gpg] https://packagecloud.io/amoshi/alligator/ubuntu/ jammy main' | tee /etc/apt/sources.list.d/alligator.list
```

### Ubuntu 24.04:
```
echo 'deb [signed-by=/usr/share/keyrings/alligator-packagecloud.gpg] https://packagecloud.io/amoshi/alligator/ubuntu/ noble main' | tee /etc/apt/sources.list.d/alligator.list
```

## Debian
### Debian 11
```
echo 'deb [signed-by=/usr/share/keyrings/alligator-packagecloud.gpg] https://packagecloud.io/amoshi/alligator/ubuntu bullseye main' | tee /etc/apt/sources.list.d/alligator.list
```

### Debian 12
```
echo 'deb [signed-by=/usr/share/keyrings/alligator-packagecloud.gpg] https://packagecloud.io/amoshi/alligator/ubuntu bookworm main' | tee /etc/apt/sources.list.d/alligator.list
```

## FreeBSD
Alligator собирается на FreeBSD из исходников (см. **Сборка** ниже). CI использует виртуальную машину VirtualBox — см. [ci-freebsd-virtualbox.md](../ci-freebsd-virtualbox.md). Официального репозитория пакетов пока нет.

# Сборка
В качестве системы сборки используется CMake. Зависимости поставляются через conan и git submodules.
Для сборки выполните следующие команды:
## Установка зависимостей:
```
git submodule sync --recursive
git submodule update --init --recursive
cd src
conan install . --build=missing -s build_type=Debug
conan build external/
```

Для CentOS 7 сначала используйте закреплённые зависимости:
```
cp ../misc/centos7/conanfile.txt ./conanfile.txt
conan install . --build=missing -s build_type=Debug
```

## Сборка alligator
```
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=build/Debug/generators/conan_toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

Не передавайте `--output-folder=build` в `conan install` при использовании `cmake_layout`: в этом случае генераторы окажутся в `build/build/<Config>/generators`, и пути выше перестанут работать.
