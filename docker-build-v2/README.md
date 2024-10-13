# Engine Docker Build v2

This directory contains work in progress next version of the Docker engine
build scripts.

To run build locally run `build.sh` script

```console
$ docker-build-v2/build.sh
USAGE: docker-build-v2/build.sh windows|linux [cmake flags...]
```

For example

```shell
docker-build-v2/build.sh windows
```

will:

1. Automatically fetch/update Docker image with engine build environment
2. Configure the release configuration of engine build
3. Compile and install engine using following paths in the repository root:
   - `.cache`: cmake cache
   - `build-windows`: compilation output
   - `build-windows/install`: ready to use installation

Because script takes cmake arguments build can be easily adjusted, e.g. to
compile linux release with Tracy support and skip building headless:

```shell
docker-build-v2/build.sh linux -DBUILD_spring-headless=OFF -DTRACY_ENABLE=ON
```
