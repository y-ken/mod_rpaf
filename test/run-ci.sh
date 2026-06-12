#!/bin/bash
#
# Build mod_rpaf on Apache 2.2 (CentOS 6) and run a functional access-control
# test that reproduces https://github.com/y-ken/mod_rpaf/pull/2 :
#
#   * a request forwarded by a trusted proxy is authorised based on the client
#     IP that mod_rpaf restores from X-Forwarded-For (not the proxy address).
#
# The script is meant to run inside a `centos:6` container with the repository
# mounted at the current working directory. It is used by both the GitHub
# Actions CI workflow and local `docker run` invocations.
#
set -eux

# ---------------------------------------------------------------------------
# 1. CentOS 6 is EOL: repoint yum from the dead mirrors to vault.centos.org
# ---------------------------------------------------------------------------
sed -e 's|^mirrorlist=|#mirrorlist=|g' \
    -e 's|^#\?baseurl=http://mirror.centos.org|baseurl=https://vault.centos.org|g' \
    -i /etc/yum.repos.d/CentOS-Base.repo

# ---------------------------------------------------------------------------
# 2. Install build + runtime dependencies
# ---------------------------------------------------------------------------
yum -y install httpd httpd-devel gcc make curl

# ---------------------------------------------------------------------------
# 3. Build and install the module
# ---------------------------------------------------------------------------
make clean
make
make install

# ---------------------------------------------------------------------------
# 4. Write the test configuration
# ---------------------------------------------------------------------------
cat > /etc/httpd/conf.d/rpaf-test.conf <<'EOF'
ServerName localhost

LoadModule rpaf_module modules/mod_rpaf-2.0.so
RPAFenable    On
RPAFproxy_ips 127.0.0.1
RPAFheader    X-Forwarded-For

# Reproduces y-ken/mod_rpaf#2: access is granted based on the client IP that
# mod_rpaf restores from X-Forwarded-For, not the trusted proxy's own address.
<Location /rpaf_test>
  Order deny,allow
  Deny  from all
  Allow from 1.1.1.1
</Location>
EOF

echo ok > /var/www/html/rpaf_test

# ---------------------------------------------------------------------------
# 5. Start Apache and wait until it accepts connections
# ---------------------------------------------------------------------------
/usr/sbin/httpd -k start
for _ in $(seq 1 10); do
    curl -s -o /dev/null http://localhost/ && break
    sleep 1
done

# ---------------------------------------------------------------------------
# 6. Assertions
# ---------------------------------------------------------------------------
fail=0
check() {
    desc="$1"; xff="$2"; want="$3"
    got=$(curl -s -o /dev/null -w '%{http_code}' \
               -H "X-Forwarded-For: ${xff}" http://localhost/rpaf_test)
    if [ "$got" = "$want" ]; then
        echo "PASS: ${desc} (X-Forwarded-For: ${xff}) -> ${got}"
    else
        echo "FAIL: ${desc} (X-Forwarded-For: ${xff}) -> expected ${want}, got ${got}"
        fail=1
    fi
}

# Forwarded client IP is in the Allow list -> 200
check "allowed client IP" "1.1.1.1" "200"
# Forwarded client IP is not in the Allow list -> 403
check "denied client IP"  "2.2.2.2" "403"

if [ "$fail" -ne 0 ]; then
    echo "----- error_log -----"
    tail -n 50 /var/log/httpd/error_log || true
    exit 1
fi

echo "All tests passed."
