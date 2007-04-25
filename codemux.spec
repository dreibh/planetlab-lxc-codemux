Summary: CoDemux - HTTP port demultiplexer
Name: codemux
Version: 0.5
Release: 1
License: Private
Group: System Environment/Base
URL: http://codeen.cs.princeton.edu/
Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

%description

%prep
%setup -q

make clean

%build
make

%install
rm -rf $RPM_BUILD_ROOT

make INSTALL_ROOT=$RPM_BUILD_ROOT install-all

# Build and install in %post so that it gets put in the right site-packages directory
#rm -rf $RPM_BUILD_ROOT/usr/lib/python*
#install -D -m 755 python/Proper.py $RPM_BUILD_ROOT/%{_datadir}/%{name}/Proper.py

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
%attr(0755,root,root) %{_initrddir}/codemux
%config /etc/codemux/codemux.conf
%attr(0755,root,root) /usr/local/planetlab/sbin/codemux

%post
chkconfig codemux reset

if [ -z "$PL_BOOTCD" ]; then
    /sbin/ldconfig
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

%doc

%changelog
* Sun Apr 22 2007 KYOUNGSOO PARK <kyoungso@park.cs.princeton.edu> - 
- Initial build.

