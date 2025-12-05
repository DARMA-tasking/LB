ARG REPO=lifflander1/vt
ARG ARCH=amd64
ARG IMAGE=wf-amd64-ubuntu-22.04-gcc-12-cpp

ARG BASE=${REPO}:${IMAGE}

FROM --platform=${ARCH} ${BASE} AS build

ARG IMAGE
ARG CACHE_ID=${IMAGE}

RUN --mount=type=cache,id=${CACHE_ID},target=/build/ccache             \
    --mount=type=cache,id=BUILD-${CACHE_ID},target=/build/LB           \
    --mount=type=secret,id=GITHUB_TOKEN,env=GITHUB_TOKEN               \
    --mount=target=/LB,rw                                              \
        /LB/ci/build_cpp.sh /LB /build &&                              \
        /LB/ci/test_cpp.sh /LB /build
