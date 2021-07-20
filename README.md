## mod_rpaf-fork - reverse proxy add forward

### Summary

Apache-2.2 module for reverse proxy forked from mod_rpaf-0.6. <br>
Set `REMOTE_ADDR`, `HTTPS`, and `HTTP_PORT` from upstream proxy environment variables.

### What is the difference from original mod_rpaf-0.6.

* Feature: Add directive RPAFsethttps. It's compatible with AWS ELB.
* Feature: Add directive RPAFsetport.
* Feature: Support for partial IP address as '192.168.' for RPAFproxy_ips.
* Bugfix: In the case of APR_HAVE_IPV6-enabled build, access control of Order/Allow/Deny does not work correctly.
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

**Note:** The option of `RPAFsetport` has limitation. It only work for one virtualhost on localhost:80, and you only send requests like
X-Forwarded-Port: 443.  
Do not use this option for the regular multi domain hosted server due to current Apache architecture.

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

Patch is available for Apache 2.4+
http://blog.77jp.net/mod_rpaf-install-apache-2-4
