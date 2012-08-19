Summary:        Reverse Proxy Add Forward module for Apache
Name:           mod_rpaf
Version:        0.6
Release:        2%{?dist}
Summary:        mod_rpaf 

Group:          System Environment/Daemons
License:        Apache Software License
URL:            https://github.com/y-ken/mod_rpaf
Source0:        mod_rpaf-%{version}.tar.gz
Source1:        rpaf.conf
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:  httpd-devel >= 2.0.38
Requires:       httpd

%description
rpaf is for backend Apache servers what mod_proxy_add_forward is for
frontend Apache servers. It does excactly the opposite of
mod_proxy_add_forward written by Ask Bjørn Hansen. It will also work
with mod_proxy that is distributed with Apache2 from version 2.0.38.

%prep
%setup -q -n mod_rpaf-%{version} 


%build
/usr/sbin/apxs -Wc,"%{optflags}" -c -n mod_rpaf-2.0.so mod_rpaf-2.0.c


%install
rm -rf %{buildroot}
mkdir -p %{buildroot}%{_libdir}/httpd/modules/
mkdir -p %{buildroot}/%{_sysconfdir}/httpd/conf.d/
install -p .libs/mod_rpaf-2.0.so %{buildroot}/%{_libdir}/httpd/modules/
install -m644 %{SOURCE1} %{buildroot}/%{_sysconfdir}/httpd/conf.d/


%clean
rm -rf %{buildroot}


%files
%defattr(-,root,root,-)
%{_libdir}/httpd/modules/mod_rpaf-2.0.so
%config(noreplace) %{_sysconfdir}/httpd/conf.d/rpaf.conf


%changelog
* Sun Aug 19 2012 Kentaro Yoshida <y.ken.studio@gmail.com>
- Improbe forward compatibility
* Sat Mar 13 2010 Scott R. Shinn <scott@atomicrocketturtle.com> 
- Initial import to Atomic
