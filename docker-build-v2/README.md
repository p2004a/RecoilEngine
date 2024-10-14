# Engine Docker Build v2

This directory contains work in progress next version of the Docker engine
build scripts.

## Local usage

To run build locally run `build.sh` script

```console
$ docker-build-v2/build.sh
USAGE: docker-build-v2/build.sh {windows|linux} [cmake_flag...]
```

For example

```shell
docker-build-v2/build.sh windows
```

will:

1. Automatically fetch/update Docker image with engine build environment from
   [GitHub packages](https://github.com/beyond-all-reason?tab=packages&repo_name=spring)
2. Configure the release configuration of engine build
3. Compile and install engine using following paths in the repository root:
   - `.cache`: cmake cache
   - `build-windows`: compilation output
   - `build-windows/install`: ready to use installation

### Custom build config

Because script takes cmake arguments build can be easily adjusted, e.g. to
compile linux release with Tracy support and skip building headless:

```shell
docker-build-v2/build.sh linux -DBUILD_spring-headless=OFF -DTRACY_ENABLE=ON
```

### Custom Docker image

Before downloading official Docker compilation image, `build.sh` will first
lookup if there exists a Docker image with tag `recoil-build-amd64-windows`
(and `-linux` for Linux). If you want to adjust the Docker build image,
test local changes, build it with that tag:

```shell
docker build -t recoil-build-amd64-windows docker-build-v2/amd64-windows
```

and `build.sh` will use it.
