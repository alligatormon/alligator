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

### Ubuntu 26.04:
```
echo 'deb [signed-by=/usr/share/keyrings/alligator-packagecloud.gpg] https://packagecloud.io/amoshi/alligator/ubuntu/ resolute main' | tee /etc/apt/sources.list.d/alligator.list
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

## macOS
CLI-пакет для Apple Silicon (arm64). Скачайте `alligator-<version>-macOS.pkg` с [GitHub Releases](https://github.com/alligatormon/alligator/releases). В Packagecloud не публикуется. `/usr/bin` закрыт SIP; установщик кладёт бинарник в `/usr/local/bin`.

```
xattr -cr alligator-*-macOS.pkg
sudo installer -pkg alligator-*-macOS.pkg -target /
alligator --version
```

Или скопируйте подписанный артефакт `alligator-macos-arm64`:

```
sudo install -m 755 alligator-macos-arm64 /usr/local/bin/alligator
xattr -cr /usr/local/bin/alligator
```

Сборка подписана ad-hoc, без нотаризации Apple. «Open» в Finder для `.app` (и карантин у скачанного файла) даёт *Apple could not verify … malware*. Ставьте из Terminal, как выше, либо разрешите pkg в **System Settings → Privacy & Security**. Нотаризация требует платного Apple Developer ID.

CI: [`.github/workflows/macos.yml`](../../.github/workflows/macos.yml) на GitHub-hosted раннерах `macos-15`.

## FreeBSD
Скачайте `alligator-<version>-FreeBSD.pkg` с [GitHub Releases](https://github.com/alligatormon/alligator/releases) и установите:

```
pkg add alligator-<version>-FreeBSD.pkg
```

Официальной записи в ports/pkg пока нет. Можно собрать из исходников (см. **Сборка** ниже).

CI: [`.github/workflows/freebsd.yml`](../../.github/workflows/freebsd.yml) запускает VM FreeBSD 14 на GitHub-hosted Ubuntu. Self-managed GitLab runner с VirtualBox опционален — см. [ci-freebsd-virtualbox.md](../ci-freebsd-virtualbox.md).

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
