Summary: synchronize Palm OS devices with Unix workstations
Name: coldsync
Version: 3.0
Release: 1
Copyright: Artistic
Group: Applications/Communications
URL: http://www.coldsync.org/
Packager: root@localhost
BuildRoot: /tmp/coldsync-3.0/
Requires: perl

%description
ColdSync is a tool for synchronizing Palm OS devices (PalmPilot, Palm V,
Qualcomm PDQ, etc.) with Unix workstations. This is an open source
project, and is free for both commercial and non-commercial use, under the
terms of the Artistic license.

%prep
rm -rf $RPM_BUILD_DIR/coldsync-3.0
tar -xzvf $RPM_SOURCE_DIR/coldsync-3.0.tar.gz

%build
cd $RPM_BUILD_DIR/coldsync-3.0
autoconf
./configure
make

%install
cd $RPM_BUILD_DIR/coldsync-3.0
make DESTDIR=$RPM_BUILD_ROOT install
cd $RPM_BUILD_ROOT
find . -type f |sed s/^\.//g > $RPM_BUILD_DIR/coldsync-3.0/buildfiles

%clean
rm -rf $RPM_BUILD_ROOT

%files -f coldsync-3.0/buildfiles
