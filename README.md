## mod_rpaf-fork - reverse proxy add forward

### Summary

Apache-2.2 module for reverse proxy forked from mod_rpaf-0.6. <br>
Set `REMOTE_ADDR`, `HTTPS`, and `HTTP_PORT` from upstream proxy environment variables.

This module targets Apache 2.2. On Apache 2.4 and later it is not needed —
use the bundled [`mod_remoteip`](https://httpd.apache.org/docs/2.4/mod/mod_remoteip.html)
instead.

### What is the difference from original mod_rpaf-0.6.

* Feature: Add directive RPAFsethttps. It's compatible with AWS ELB.
* Feature: Add directive RPAFsetport.
* Feature: Support for partial IP address as '192.168.' for RPAFproxy_ips.
* Bugfix: In the case of APR_HAVE_IPV6-enabled build, access control of Order/Allow/Deny does not work correctly.
* Bugfix: `RPAFsethttps` now also sets the SSL flag used by `RewriteCond %{HTTPS}`, not only `%{ENV:HTTPS}` (ported from gnif/mod_rpaf#6).
* Bugfix: Behind a chain of proxies the real client IP (the last forwarded entry that is not a trusted proxy) is now used, instead of always taking the last entry.
* Bugfix: Invalid `X-Forwarded-For` entries are validated and skipped instead of being trusted blindly.
* Bugfix: `RPAFsetport` no longer mutates the shared server_rec, so it is safe to use with multiple virtualhosts.
* Support of httpd 1.3 was deleted.

### Install with rpm package for RedHat/CentOS 6.x

````
yum localinstall http://y-ken.github.io/package/centos/6/x86_64/mod_rpaf-fork-0.6-5.el6.x86_64.rpm
````

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

### Compile Debian/Ubuntu Package and Install

````
sudo apt-get install build-essential apache2-threaded-dev yada
dpkg-buildpackage -b
sudo dpkg -i ../libapache2-mod-rpaf_X.X-X.X_XXX.deb
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

This module targets the Apache 2.2 API, so the tests build and run it inside a
CentOS 6 container (which ships Apache 2.2). With Docker available you can run
the same suite that CI runs:

````
docker run --rm --platform linux/amd64 -v "$PWD":/work -w /work \
  centos:6 bash test/run-ci.sh
````

It builds the module, starts Apache, and checks `X-Forwarded-For` access
control, real-client resolution behind a chain of proxies, and
`RewriteCond %{HTTPS}` handling. The same command runs automatically on push to
`master` and on pull requests via GitHub Actions (`.github/workflows/ci.yml`).
A VS Code Dev Container (`.devcontainer/`) is also provided for editing with the
build/test toolchain one command away.

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

For Apache 2.4 and later, use the bundled `mod_remoteip` instead of this module.

An old community patch for building this module on Apache 2.4+ also exists
(unsupported here):
http://blog.77jp.net/mod_rpaf-install-apache-2-4
