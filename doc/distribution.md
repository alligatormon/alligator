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
Install prerequisites and import the Packagecloud GPG key. On Debian/Ubuntu 22.04+, prefer `signed-by` instead of deprecated `apt-key`:

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
Apple Silicon (arm64) CLI package. Download `alligator-<version>-macOS.pkg` from [GitHub Releases](https://github.com/alligatormon/alligator/releases). It is not published to Packagecloud. `/usr/bin` is SIP-protected; the installer puts the binary in `/usr/local/bin`.

```
xattr -cr alligator-*-macOS.pkg
sudo installer -pkg alligator-*-macOS.pkg -target /
alligator --version
```

Or copy the signed `alligator-macos-arm64` artifact:

```
sudo install -m 755 alligator-macos-arm64 /usr/local/bin/alligator
xattr -cr /usr/local/bin/alligator
```

The build is ad-hoc signed, not Apple-notarized. Finder “Open” on a `.app` (or a quarantined download) shows *Apple could not verify … malware*. Install from Terminal as above, or allow the pkg under **System Settings → Privacy & Security**. Notarization needs a paid Apple Developer ID.

CI: [`.github/workflows/macos.yml`](../.github/workflows/macos.yml) on GitHub-hosted `macos-15` runners.

## FreeBSD
Download `alligator-<version>-FreeBSD.pkg` from [GitHub Releases](https://github.com/alligatormon/alligator/releases) and install:

```
pkg add alligator-<version>-FreeBSD.pkg
```

There is no official FreeBSD ports/pkg repository entry yet. You can also build from source (see **Build** below).

CI: [`.github/workflows/freebsd.yml`](../.github/workflows/freebsd.yml) runs a FreeBSD 14 VM on a GitHub-hosted Ubuntu runner. A self-managed VirtualBox GitLab runner is optional — see [ci-freebsd-virtualbox.md](ci-freebsd-virtualbox.md).

# Build
CMake is used as build system. Dependencies are supplied with conan and git submodules.
To build use these commands:
## Dependency installation:
```
git submodule sync --recursive
git submodule update --init --recursive
cd src
conan install . --build=missing -s build_type=Debug
conan build external/
```

For CentOS 7, use the pinned deps first:
```
cp ../misc/centos7/conanfile.txt ./conanfile.txt
conan install . --build=missing -s build_type=Debug
```

## Build alligator
```
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=build/Debug/generators/conan_toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

Do not pass `--output-folder=build` to `conan install` when using `cmake_layout`: that nests
generators under `build/build/<Config>/generators` and breaks the paths above.
