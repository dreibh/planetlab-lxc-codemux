%define name codemux 
%define version 0.1
%define release 4%{?pldistro:.%{pldistro}}%{?date:.%{date}}

Summary: CoDemux - HTTP port DeMux
Name: %{name} 
Version: %{version}
Release: %{release}
License: Private
Group: System Environment/Base
Source: %{name}-%{version}.tar.gz
Vendor: PlanetLab
Packager: PlanetLab Central <support@planet-lab.org>
Distribution: PlanetLab 3.0
URL: http://codeen.cs.princeton.edu/
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
Requires: vnet

%description
codemux is a privileged port (80) sharing service that passes traffic to and from slices via localhost ports.


%prep
%setup -q

make clean

%build
make

%install
rm -rf $RPM_BUILD_ROOT

make INSTALL_ROOT=$RPM_BUILD_ROOT install

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(0644,root,root)
%attr(0755,root,root) %{_initrddir}/codemux
%config /etc/codemux/codemux.conf
%attr(0755,root,root) /usr/local/planetlab/sbin/codemux

%post
chkconfig codemux reset

if [ -z "$PL_BOOTCD" ]; then
    /etc/init.d/codemux restart
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
* Sun Apr 22 2007 KYOUNGSOO PARK <kyoungso@park.cs.princeton.edu> - 
- Initial build.

