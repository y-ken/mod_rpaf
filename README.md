## mod_rpaf - reverse proxy add forward

### Summary

Sets `REMOTE_ADDR`, `HTTPS`, and `HTTP_PORT` to the values provided by an upstream proxy.

### What is the difference from original mod_rpaf-0.6.

* Feature: Add directive RPAF_SetHTTPS.
* Feature: Add directive RPAF_SetPort.
* Feature: Support for partial IP address as '10.1.' for RPAFproxy_ips.
* Feature: Support for rewrite for passing CIDRs in RPAF_ProxyIPs.
* Bugfix: In the case of APR_HAVE_IPV6-enabled build, access control of Order/Allow/Deny does not work correctly.
* Support of httpd 1.3 was deleted.

### Compile Debian/Ubuntu Package and Install

    sudo apt-get install build-essential apache2-threaded-dev yada
    dpkg-buildpackage -b
    sudo dpkg -i ../libapache2-mod-rpaf_X.X-X.X_XXX.deb

### Compile and Install for RedHat/CentOS

    yum install httpd-devel
    make
    make install

### Install with rpm for RedHat/CentOS 5.x

    rpm -ivh https://github.com/y-ken/mod_rpaf/raw/master/rpm/centos5/mod_rpaf-0.6-1.x86_64.rpm

### Install with rpm for RedHat/CentOS 6.x

    rpm -ivh https://github.com/y-ken/mod_rpaf/raw/master/rpm/centos6/mod_rpaf-0.6-1.el6.x86_64.rpm

### Configuration Directives

    RPAF_Enable      (On|Off)           - Enable reverse proxy add forward

    RPAF_ProxyIPs    127.0.0.1 10.0.0.1 - What IPs to adjust requests for

    RPAF_Header      X-Forwarded-For    - The header to use for the real IP address.

    RPAF_SetHostName (On|Off)           - Update vhost name so ServerName & 
                                          ServerAlias work

    RPAF_SetHTTPS    (On|Off)           - Set the HTTPS environment variable
                                          to the header value contained in
                                          X-HTTPS, or X-Forwarded-HTTPS.
                                          Also work X-Forwarded-Proto is https.

    RPAF_SetPort     (On|Off)           - Set the server port to the header
                                          value contained in X-Port, or
                                          X-Forwarded-Port.

## Example Configuration

    LoadModule        rpaf_module modules/mod_rpaf-2.0.so
    RPAF_Enable       On
    RPAF_ProxyIPs     127.0.0.1 10.0.0.1/28
    RPAF_Header       X-Forwarded-For
    RPAF_SetHostName  On
    RPAF_SetHTTPS     On
    RPAF_SetPort      On
  
## Authors

* Thomas Eibner <thomas@stderr.net>
* Takashi Takizawa <taki@cyber.email.ne.jp>
* Geoffrey McRae <gnif@xbmc.org>
* Kentaro YOSHIDA <y.ken.studio@gmail.com>

## License and distribution

This software is licensed under the [Apache License 2.0](http://www.apache.org/licenses/LICENSE-2.0). The
latest version is available [from GitHub](http://github.com/y-ken/mod_rpaf)

## Footnote

It is based on following project.
* https://github.com/ttkzw/mod_rpaf-0.6
* https://github.com/gnif/mod_rpaf
