#!/bin/bash
#
# Build mod_rpaf and run a functional access-control test that reproduces
# https://github.com/y-ken/mod_rpaf/pull/2 :
#
#   * a request forwarded by a trusted proxy is authorised based on the client
#     IP that mod_rpaf restores from X-Forwarded-For (not the proxy address).
#
# The script runs inside a CentOS container with the repository mounted at the
# current working directory, and works on both CentOS 6 (Apache 2.2) and
# CentOS 7 (Apache 2.4). It is used by both the GitHub Actions CI workflow and
# local `docker run` invocations.
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
# 2. Install build + runtime dependencies ('which' is required by the Makefile
#    and is not present on the minimal CentOS 7 image)
# ---------------------------------------------------------------------------
yum -y install httpd httpd-devel gcc make which curl

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

# Force a single child process so the server_scheme leak test is deterministic:
# every request is handled by the same process and thus the same server_rec.
StartServers    1
MinSpareServers 1
MaxSpareServers 1
ServerLimit     1
MaxClients      1

LoadModule rpaf_module modules/mod_rpaf-2.0.so
RPAFenable      On
RPAFproxy_ips   127.0.0.1
RPAFheader      X-Forwarded-For
RPAFsethttps    On
RPAFsethostname On

# Reproduces y-ken/mod_rpaf#2: access is granted based on the client IP that
# mod_rpaf restores from X-Forwarded-For, not the trusted proxy's own address.
<Location /rpaf_test>
  Order deny,allow
  Deny  from all
  Allow from 1.1.1.1
</Location>

# Reproduces gnif/mod_rpaf#6: mod_rewrite resolves %{HTTPS} through the
# ssl_is_https optional function, so X-Forwarded-Proto: https must make
# RewriteCond %{HTTPS} match directly (not only %{ENV:HTTPS}).
RewriteEngine On
RewriteCond %{HTTPS} =on
RewriteRule ^/https_check$ /https_is_on  [L]
RewriteRule ^/https_check$ /https_is_off [L]

# RPAFsethostname: when X-Forwarded-Host carries a comma-separated list the
# last entry must become the effective Host.
RewriteCond %{HTTP_HOST} =second.example
RewriteRule ^/host_check$ /host_is_second [L]
RewriteRule ^/host_check$ /host_is_other  [L]
EOF

echo ok     > /var/www/html/rpaf_test
echo on     > /var/www/html/https_is_on
echo off    > /var/www/html/https_is_off
echo second > /var/www/html/host_is_second
echo other  > /var/www/html/host_is_other
mkdir -p /var/www/html/dir

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

# X-Forwarded-For access control (y-ken/mod_rpaf#2)
# Forwarded client IP is in the Allow list -> 200
check "allowed client IP" "1.1.1.1" "200"
# Forwarded client IP is not in the Allow list -> 403
check "denied client IP"  "2.2.2.2" "403"

# Multi-proxy chain: a trailing trusted-proxy entry is skipped and the real
# client (1.1.1.1) is used for access control.
check "multi-proxy real client" "1.1.1.1, 127.0.0.1" "200"
# Bogus forwarded entries are rejected, leaving the valid client IP in effect.
check "invalid entry skipped"    "1.1.1.1, not-an-ip" "200"

# %{HTTPS} via mod_rewrite (gnif/mod_rpaf#6). A trusted proxy must supply
# X-Forwarded-For for mod_rpaf to process the request, so it is always sent.
checkhttps() {
    desc="$1"; proto="$2"; want="$3"
    if [ -n "$proto" ]; then
        got=$(curl -s -H "X-Forwarded-For: 1.1.1.1" \
                   -H "X-Forwarded-Proto: ${proto}" http://localhost/https_check)
    else
        got=$(curl -s -H "X-Forwarded-For: 1.1.1.1" http://localhost/https_check)
    fi
    if [ "$got" = "$want" ]; then
        echo "PASS: ${desc} -> %{HTTPS}=${got}"
    else
        echo "FAIL: ${desc} -> expected %{HTTPS}=${want}, got ${got}"
        fail=1
    fi
}

# X-Forwarded-Proto: https makes RewriteCond %{HTTPS} match
checkhttps "HTTPS on via X-Forwarded-Proto: https" "https" "on"
# No forwarded proto leaves %{HTTPS} off
checkhttps "HTTPS off without X-Forwarded-Proto"    ""      "off"

# Minor 2: a non-truthy X-Forwarded-HTTPS value must not be treated as https.
body=$(curl -s -H "X-Forwarded-For: 1.1.1.1" -H "X-Forwarded-HTTPS: off" \
            http://localhost/https_check)
if [ "$body" = "off" ]; then
    echo "PASS: X-Forwarded-HTTPS: off is not treated as https -> %{HTTPS}=off"
else
    echo "FAIL: X-Forwarded-HTTPS: off -> expected %{HTTPS}=off, got ${body}"
    fail=1
fi

# Minor 1: the last entry of a comma-separated X-Forwarded-Host wins.
body=$(curl -s -H "X-Forwarded-For: 1.1.1.1" \
            -H "X-Forwarded-Host: first.example, second.example" \
            http://localhost/host_check)
if [ "$body" = "second" ]; then
    echo "PASS: last X-Forwarded-Host entry becomes the Host -> ${body}.example"
else
    echo "FAIL: X-Forwarded-Host list -> expected Host second.example, got ${body}"
    fail=1
fi

# Fix F: a https request must not leak server_scheme into later plain requests
# handled by the same child process. After a https-forwarded request, the
# mod_dir redirect for /dir (sent without a forwarded proto) must stay http://.
curl -s -o /dev/null -H "X-Forwarded-For: 1.1.1.1" -H "X-Forwarded-Proto: https" \
     http://localhost/https_check
redir=$(curl -s -o /dev/null -w '%{redirect_url}' \
             -H "X-Forwarded-For: 1.1.1.1" http://localhost/dir)
case "$redir" in
    http://*)  echo "PASS: server_scheme not leaked across requests -> ${redir}" ;;
    *)         echo "FAIL: server_scheme leaked, redirect was -> ${redir}"; fail=1 ;;
esac

if [ "$fail" -ne 0 ]; then
    echo "----- error_log -----"
    tail -n 50 /var/log/httpd/error_log || true
    exit 1
fi

echo "All tests passed."
