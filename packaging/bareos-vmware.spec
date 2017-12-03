#
# spec file for package  bareos-vmware-plugin
#
# Copyright (c) 2015-2017 Bareos GmbH & Co KG

# bareos-vadp-dumper must be main project,
# as the package with debug info gets the name of the main package.
Name:           bareos-vadp-dumper
Version:        0.0.0
Release:        0
License:        AGPL-3.0
URL:            http://www.bareos.org/
Vendor:         The Bareos Team
%define         sourcename bareos-vmware-%{version}
Source:         %{sourcename}.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-build

BuildRequires:  bareos-vmware-vix-disklib-devel
BuildRequires:  cmake
BuildRequires:  gcc-c++
%if 0%{?suse_version}
BuildRequires:  libjansson-devel
%else
BuildRequires:  jansson-devel
%endif

#%%package -n     bareos-vadp-dumper
Summary:        VADP Dumper - vStorage APIs for Data Protection Dumper program
Group:          Productivity/Archiving/Backup
Requires:       bareos-vmware-vix-disklib
%description -n bareos-vadp-dumper
Uses vStorage API to connect to VMWare and dump data like virtual disks snapshots
to be used by other programs.



%package -n     bareos-vmware-plugin
Summary:        Bareos VMware plugin
Group:          Productivity/Archiving/Backup
Requires:       bareos-vadp-dumper
Requires:       bareos-filedaemon-python-plugin >= 15.2
Requires:       python-pyvmomi
%if 0%{?suse_version} == 1110 
Requires:       python-ordereddict
%endif
%description -n bareos-vmware-plugin
Uses the VMware API to take snapshots of running VMs and takes
full and incremental backup so snapshots. Restore of a snapshot
is currently supported to the origin VM.


#%%package -n     bareos-vmware-vix-disklib
#Summary:        VMware vix disklib distributable libraries
#Group:          Productivity/Archiving/Backup
#Url:            https://www.vmware.com/support/developer/vddk/
#License:        VMware
#AutoReqProv:    no
#%%description -n bareos-vmware-vix-disklib
#The Virtual Disk Development Kit (VDDK) is a collection of C/C++ libraries,
#code samples, utilities, and documentation to help you create and access
#VMware virtual disk storage. The VDDK is useful in conjunction with the
#vSphere API for writing backup and recovery software, or similar applications.
#This package only contains the distributable code to comply with
#https://communities.vmware.com/viewwebdoc.jspa?documentID=DOC-10141

%package -n     bareos-vmware-plugin-compat
Summary:        Bareos VMware plugin compatibility
Group:          Productivity/Archiving/Backup
Requires:       bareos-vmware-plugin
%description -n bareos-vmware-plugin-compat
Keeps bareos/plugins/vmware_plugin subdirectory, which have been used in Bareos <= 16.2.



#PreReq:
#Provides:

# Note: __requires_exclude only works for dists with rpm version >= 4.9
#       SLES12 has suse_version 1315, SLES11 has 1110
%if 0%{?rhel_version} >= 700 || 0%{?centos_version} >= 700 || 0%{?fedora_version} >= 16 || 0%{?suse_version} >= 1110
%global __requires_exclude ^.*libvixDiskLib.*$
%else
%define _use_internal_dependency_generator 0
%define our_find_requires %{_builddir}/%{name}-%{version}/find_requires
%endif

# exclude libvixDiskLib from autogenerated requires on older dists:
%if ! ( 0%{?rhel_version} >= 700 || 0%{?centos_version} >= 700 || 0%{?fedora_version} >= 16 || 0%{?suse_version} >= 1210 )
cat <<EOF >%{our_find_requires}
#!/bin/sh
exec %{__find_requires} | /bin/egrep -v '^.*libvixDiskLib.*$'
exit 0
EOF
chmod +x %{our_find_requires}
%define __find_requires %{our_find_requires}
%endif


%prep
%setup -q -n %{sourcename}

%build
cmake -DCMAKE_INSTALL_PREFIX:PATH=/usr
make

%install
%make_install


%files -n bareos-vadp-dumper
%defattr(-,root,root)
%{_sbindir}/bareos_vadp_dumper*
%doc LICENSE.vadp

%files -n bareos-vmware-plugin
%defattr(-,root,root)
%dir %{_libdir}/bareos/
%{_libdir}/bareos/plugins/
%{_sbindir}/vmware_cbt_tool.py
%doc AUTHORS LICENSE README.md

#%%files -n bareos-vmware-vix-disklib
#/usr/lib/vmware-vix-disklib/lib64

%files -n bareos-vmware-plugin-compat
%defattr(-,root,root)
%{_libdir}/bareos/plugins/vmware_plugin/

%changelog

