## mod_rpaf-fork - reverse proxy add forward

### Summary

Reverse proxy module forked from mod_rpaf-0.6. <br>
Set `REMOTE_ADDR`, `HTTPS`, and `HTTP_PORT` from upstream proxy environment variables.

It builds and runs on both **Apache 2.2** (e.g. CentOS 6) and **Apache 2.4**
(e.g. CentOS 7); the version-specific client-address API is selected at compile
time. On Apache 2.4 the bundled
[`mod_remoteip`](https://httpd.apache.org/docs/2.4/mod/mod_remoteip.html) is a
good alternative, but this module is still useful there for features it lacks,
such as `RPAFsetport` and partial-IP `RPAFproxy_ips`.

### What is the difference from original mod_rpaf-0.6.

* Feature: Add directive RPAFsethttps. It's compatible with AWS ELB.
* Feature: Add directive RPAFsetport.
* Feature: Support for partial IP address as '192.168.' for RPAFproxy_ips.
* Bugfix: In the case of APR_HAVE_IPV6-enabled build, access control of Order/Allow/Deny does not work correctly.
* Bugfix: `RPAFsethttps` now also sets the SSL flag used by `RewriteCond %{HTTPS}`, not only `%{ENV:HTTPS}` (ported from gnif/mod_rpaf#6).
* Bugfix: Behind a chain of proxies the real client IP (the last forwarded entry that is not a trusted proxy) is now used, instead of always taking the last entry.
* Bugfix: Invalid `X-Forwarded-For` entries are validated and skipped instead of being trusted blindly.
* Bugfix: `RPAFsetport` no longer mutates the shared server_rec, so it is safe to use with multiple virtualhosts.
* Bugfix: `RPAFsethttps` no longer leaks the `https` scheme into later plain-HTTP requests on the same server (the scheme is reset per request).
* Bugfix: `RPAFsethttps` validates the forwarded HTTPS header value, so a value like `off` is no longer treated as HTTPS.
* Bugfix: A comma-separated `X-Forwarded-Host` now uses its last entry as the effective Host.
* Feature: Builds and runs on Apache 2.4 (e.g. CentOS 7) as well as Apache 2.2, selecting the client-address API at compile time (closes #3).
* Support of httpd 1.3 was deleted.

### Install the prebuilt RPM (RedHat/CentOS 6 and 7)

Prebuilt RPMs are published on GitHub Pages for both CentOS 6 (Apache 2.2,
`.el6`) and CentOS 7 (Apache 2.4, `.el7`). Install directly from the URL:

````
# CentOS 6 (Apache 2.2)
yum localinstall http://y-ken.github.io/mod_rpaf/centos/6/x86_64/mod_rpaf-fork-0.7-1.el6.x86_64.rpm

# CentOS 7 (Apache 2.4)
yum localinstall http://y-ken.github.io/mod_rpaf/centos/7/x86_64/mod_rpaf-fork-0.7-1.el7.x86_64.rpm
````

### Install the prebuilt .deb (Debian/Ubuntu)

````
wget http://y-ken.github.io/mod_rpaf/ubuntu/libapache2-mod-rpaf-fork_0.7-1_amd64.deb
sudo apt install ./libapache2-mod-rpaf-fork_0.7-1_amd64.deb
````

The package installs the module and enables it (`a2enmod rpaf`); edit
`/etc/apache2/mods-available/rpaf.conf` for your proxy and reload Apache.

### Build the packages yourself with Docker

The same scripts CI runs build each package inside the matching container; the
artifacts land in `dist/`:

````
# RPMs (.el6 / .el7)
docker run --rm --platform linux/amd64 -v "$PWD":/work -w /work \
  centos:6 bash packaging/build-rpm.sh
docker run --rm --platform linux/amd64 -v "$PWD":/work -w /work \
  centos:7 bash packaging/build-rpm.sh

# Debian/Ubuntu .deb
docker run --rm --platform linux/amd64 -v "$PWD":/work -w /work \
  ubuntu:22.04 bash packaging/build-deb.sh
````

Pushing a version tag (`v*`) builds all three packages and publishes them to
the `gh-pages` branch automatically (`.github/workflows/release.yml`), so the
install URLs above always serve the latest tagged release.

### Compile and Install for RedHat/CentOS

````
yum install httpd-devel
apxs -i -c -n mod_rpaf-2.0.so mod_rpaf-2.0.c
````

or simply try:

````
yum install httpd-devel
make
make install
````

### Configuration Directives

````
RPAFenable      (On|Off)           - Enable reverse proxy add forward

RPAFproxy_ips   192.168. 10.0.0.   - What IPs to adjust requests for.

RPAFheader      X-Forwarded-For    - The header to use for the real IP address.

RPAFsetHostname (On|Off)           - Update vhost name so ServerName & ServerAlias work

RPAFsethttps    (On|Off)           - Set the HTTPS environment variable to the header value 
                                     contained in X-HTTPS, or X-Forwarded-HTTPS. (experimental)
                                     Also work with X-Forwarded-Proto value were https.

RPAFsetport     (On|Off)           - Set the server port to the header value 
                                     contained in X-Port, or X-Forwarded-Port.
````

**Note:** `RPAFsetport` now applies the forwarded port per request (via
`r->parsed_uri.port`) instead of mutating the shared server configuration, so
the previous limitation of working with only a single virtualhost no longer
applies.

## Example Configuration

````
LoadModule       rpaf_module modules/mod_rpaf-2.0.so
RPAFenable       On
RPAFproxy_ips    192.168. 10.0.0.
RPAFheader       X-Forwarded-For
RPAFsetHostname  Off
RPAFsethttps     Off
RPAFsetport      Off
````

## Testing

The tests build and run the module inside a CentOS container. With Docker
available you can run the same suite that CI runs, on Apache 2.2 (CentOS 6) and
Apache 2.4 (CentOS 7):

````
docker run --rm --platform linux/amd64 -v "$PWD":/work -w /work \
  centos:6 bash test/run-ci.sh
docker run --rm --platform linux/amd64 -v "$PWD":/work -w /work \
  centos:7 bash test/run-ci.sh
````

It builds the module, starts Apache, and checks `X-Forwarded-For` access
control, real-client resolution behind a chain of proxies, and
`RewriteCond %{HTTPS}` handling. Both containers run automatically as a matrix
on push to `master` and on pull requests via GitHub Actions
(`.github/workflows/ci.yml`). A VS Code Dev Container (`.devcontainer/`) is also
provided for editing with the build/test toolchain one command away.

## Authors

* Thomas Eibner <thomas@stderr.net>
* Takashi Takizawa <taki@cyber.email.ne.jp>
* Geoffrey McRae <gnif@xbmc.org>
* Kentaro YOSHIDA <y.ken.studio@gmail.com>

## License and distribution

This software is licensed under the [Apache License 2.0](http://www.apache.org/licenses/LICENSE-2.0). The
latest version is available [from GitHub](http://github.com/y-ken/mod_rpaf)

## Footnote

It is forked following projects.
* http://stderr.net/apache/rpaf/
* https://github.com/ttkzw/mod_rpaf-0.6
* https://github.com/gnif/mod_rpaf

## Appendix

This module now builds on Apache 2.4 directly, so the old external patch is no
longer required. On Apache 2.4 the bundled `mod_remoteip` is also an option if
you do not need this module's extra features (`RPAFsetport`, partial-IP
`RPAFproxy_ips`, etc.).
