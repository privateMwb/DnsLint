# ============================================================
# .clusterfuzzlite/Dockerfile
# Base image for building the fuzz harnesses under ClusterFuzzLite.
# Dependabot keeps the pinned digest current.
# ============================================================

FROM gcr.io/oss-fuzz-base/base-builder@sha256:ed4aefea5aa72bb8c133576dd13a1417e0fc6eeb27cc0fc7f9880e7e13293b07

COPY . $SRC/DnsLint
COPY .clusterfuzzlite/build.sh $SRC/

WORKDIR $SRC/DnsLint
