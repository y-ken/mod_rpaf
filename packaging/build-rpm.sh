#!/bin/bash
#
# Build a binary RPM of mod_rpaf-fork inside a CentOS container.
#
# Works on both CentOS 6 (Apache 2.2 -> .el6 rpm) and CentOS 7 (Apache 2.4 ->
# .el7 rpm). The spec's `Release: N%{?dist}` produces the right .elN suffix; we
# pass the dist macro explicitly so it is correct even on a minimal image that
# does not define %{?dist}.
#
# Usage (from the repository root, same pattern as test/run-ci.sh):
#
#     docker run --rm --platform linux/amd64 -v "$PWD":/work -w /work \
#       centos:6 bash packaging/build-rpm.sh
#     docker run --rm --platform linux/amd64 -v "$PWD":/work -w /work \
#       centos:7 bash packaging/build-rpm.sh
#
# The resulting RPMs are copied to ./dist/ on the mounted workspace.
#
set -eux

# ---------------------------------------------------------------------------
# 1. CentOS 6 and 7 are EOL: repoint yum from the dead mirrors to vault
# ---------------------------------------------------------------------------
EL=$(rpm -E %{rhel})
if [ "$EL" = "6" ]; then
    sed -e 's|^mirrorlist=|#mirrorlist=|g' \
        -e 's|^#\?baseurl=http://mirror.centos.org|baseurl=https://vault.centos.org|g' \
        -i /etc/yum.repos.d/CentOS-Base.repo
else
    sed -e 's|^mirrorlist=|#mirrorlist=|g' \
        -e 's|^#\?baseurl=http://mirror.centos.org/centos/$releasever|baseurl=http://vault.centos.org/7.9.2009|g' \
        -i /etc/yum.repos.d/CentOS-Base.repo
fi

# ---------------------------------------------------------------------------
# 2. Install build dependencies (rpm-build + the same toolchain the module
#    needs: apxs ships in httpd-devel)
# ---------------------------------------------------------------------------
yum -y install httpd-devel gcc make which rpm-build tar gzip

# ---------------------------------------------------------------------------
# 3. Assemble the source tarball the spec expects:
#    Source0 = mod_rpaf-<version>.tar.gz containing mod_rpaf-<version>/
# ---------------------------------------------------------------------------
VERSION=$(awk '/^Version:/{print $2; exit}' mod_rpaf-fork.spec)

TOPDIR=$(mktemp -d)
mkdir -p "${TOPDIR}"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

SRCDIR=$(mktemp -d)
PKGDIR="${SRCDIR}/mod_rpaf-${VERSION}"
mkdir -p "${PKGDIR}"
cp mod_rpaf-2.0.c "${PKGDIR}/"
tar czf "${TOPDIR}/SOURCES/mod_rpaf-${VERSION}.tar.gz" -C "${SRCDIR}" "mod_rpaf-${VERSION}"
cp rpaf.conf "${TOPDIR}/SOURCES/"

# ---------------------------------------------------------------------------
# 4. Build the binary RPM (.elN suffix forced via the dist macro)
# ---------------------------------------------------------------------------
rpmbuild \
    --define "_topdir ${TOPDIR}" \
    --define "dist .el${EL}" \
    -bb mod_rpaf-fork.spec

# ---------------------------------------------------------------------------
# 5. Publish the artifacts back onto the mounted workspace
# ---------------------------------------------------------------------------
mkdir -p dist
find "${TOPDIR}/RPMS" -name '*.rpm' -exec cp -v {} dist/ \;
ls -l dist/
