%define name codemux 
%define version 0.1
%define taglevel 14

%define release %{taglevel}%{?pldistro:.%{pldistro}}%{?date:.%{date}}

Summary: CoDemux - HTTP port DeMux
Name: %{name} 
Version: %{version}
Release: %{release}
License: Private
Group: System Environment/Base
Source: %{name}-%{version}.tar.gz
Vendor: PlanetLab
Packager: PlanetLab Central <support@planet-lab.org>
Distribution: PlanetLab %{plrelease}
URL: %{SCMURL}
#URL: http://codeen.cs.princeton.edu/
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
#Requires: vnet

%description
codemux is a privileged port (80) sharing service that passes traffic to and from slices via localhost ports.


%prep
%setup -q

make clean

%build
make RPM_VERSION=%{version}.%{taglevel}

%install
rm -rf $RPM_BUILD_ROOT
make INSTALL_ROOT=$RPM_BUILD_ROOT install
install -D -m 644 codemux.logrotate $RPM_BUILD_ROOT/%{_sysconfdir}/logrotate.d/codemux

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(0644,root,root)
%attr(0755,root,root) %{_initrddir}/codemux
%config(noreplace) /etc/codemux/codemux.conf
%attr(0755,root,root) /usr/sbin/codemux
%{_sysconfdir}/logrotate.d/codemux

%post
chkconfig --add codemux

if [ -z "$PL_BOOTCD" ]; then
    /etc/init.d/codemux condrestart
fi

%preun
if [ "$1" = 0 ]; then
    # erase, not upgrade
    chkconfig --del codemux

    # stop daemon if its currently running
    if [ "`/etc/init.d/codemux status`" = "running" ]; then
	/etc/init.d/codemux stop
    fi
fi

%changelog
* Tue Mar 09 2010 S.Çağlar Onur <caglar@cs.princeton.edu> - CoDemux-0.1-14
- introduce an IP field

* Tue Dec 02 2008 Daniel Hokka Zakrisson <daniel@hozac.com> - CoDemux-0.1-13
- Add condrestart to the initscript and add a way to limit codemux to one IP.

* Fri Jun 06 2008 Stephen Soltesz <soltesz@cs.princeton.edu> - CoDemux-0.1-12
- 
- KyoungSoo added fix to prevent failure with new compilers
- 

* Fri May 09 2008 Stephen Soltesz <soltesz@cs.princeton.edu> - CoDemux-0.1-11
- 

* Thu Apr 24 2008 Faiyaz Ahmed <faiyaza@cs.princeton.edu> - CoDemux-0.1-10
- 
- Examples in conf file are enough.  Removed PLC specific entries.
- 

* Fri Mar 28 2008 Faiyaz Ahmed <faiyaza@cs.princeton.edu> - CoDemux-0.1-7 CoDemux-0.1-8
- 

* Sun Apr 22 2007 KYOUNGSOO PARK <kyoungso@park.cs.princeton.edu> - 
- Initial build.

