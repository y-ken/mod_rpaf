#!/bin/bash
#
# Build a Debian/Ubuntu .deb of mod_rpaf-fork inside an Ubuntu container.
#
# Usage (from the repository root, same pattern as packaging/build-rpm.sh):
#
#     docker run --rm --platform linux/amd64 -v "$PWD":/work -w /work \
#       ubuntu:22.04 bash packaging/build-deb.sh
#
# The resulting .deb is copied to ./dist/ on the mounted workspace.
#
set -eux

export DEBIAN_FRONTEND=noninteractive

# ---------------------------------------------------------------------------
# 1. Install build dependencies (apxs2 ships in apache2-dev)
# ---------------------------------------------------------------------------
apt-get update
apt-get install -y --no-install-recommends \
    build-essential debhelper dh-apache2 apache2-dev fakeroot

# ---------------------------------------------------------------------------
# 2. dpkg-buildpackage builds in-tree and writes the .deb to the parent
#    directory, so build from a clean copy to keep the mounted workspace tidy.
# ---------------------------------------------------------------------------
BUILD=$(mktemp -d)
cp -a debian mod_rpaf-2.0.c rpaf.conf "${BUILD}/"
cd "${BUILD}"

# Binary-only build, unsigned (-b -us -uc).
dpkg-buildpackage -b -us -uc

# ---------------------------------------------------------------------------
# 3. Publish the artifact back onto the mounted workspace
# ---------------------------------------------------------------------------
mkdir -p "${OLDPWD}/dist"
cp -v ../*.deb "${OLDPWD}/dist/"
ls -l "${OLDPWD}/dist/"
