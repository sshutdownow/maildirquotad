Name: mailquotad
Version: 0.2
Release: 1%{?dist}
Summary: maildirquotad is daemon that checks mailbox's quota on fly.

Group: System Environment/Daemons
License: GPLv2
URL: http://code.google.com/p/maildirquotad/
Source0: http://maildirquotad.googlecode.com/files/%{name}-%{version}.tar.gz
Source1: %{name}.init
Source2: %{name}.conf
BuildArch: noarch
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

Requires: perl

%description
maildirquotad is used in conjunction with exim. exim has no builtin facility
for checking the quotas of a mailbox at RCPT TO time. And when you have many MX
servers and only one mailhub that stores mail data it became a real problem.
Say, MX server has received mail, but mailhub  defers it because recipient's
mailbox is full. But MX already has said to remote server that mail had been
successfully delivered.
 
%prep
%setup -q -n %{name}-%{version}

%build
#make %{?_smp_mflags}

%install
rm -rf %{buildroot}

install -d %{buildroot}%{_initrddir}
install -d %{buildroot}%{_sysconfdir}/sysconfig
install -d %{buildroot}%{_sbindir}
install -d %{buildroot}%{_localstatedir}/run/quotad

install -m0755 -o root -g root %{name} %{buildroot}%{_sbindir}/%{name}
install -m0755 -o root -g root %{SOURCE1} %{buildroot}%{_initrddir}/%{name}
install -m0644 -o root -g root %{SOURCE2} %{buildroot}%{_sysconfdir}/sysconfig/%{name}

%post
if [ $1 = 1 ]; then
    /sbin/chkconfig --add mailquotad >/dev/null 2>&1 || :
fi
    
%preun
if [ $1 = 0 ]; then
    /sbin/chkconfig --del mailquotad >/dev/null 2>&1 || :
    [ -f /var/lock/subsys/mailquotad ] && %{_initrddir}/mailquotad stop >/dev/null
fi
exit 0
          
%postun
if [ "$1" -ge 1 ]; then
    %{_initrddir}/mailquotad condrestart >/dev/null 2>&1 || :
fi
          
%clean
rm -rf %{buildroot}


%files
%defattr(-,root,root,-)
#%doc GPL INSTALL TODO README
%attr(0755,root,root) %{_initrddir}/%{name}
%attr(0755,root,root) %{_sbindir}/%{name}
%attr(0644,root,root) %config(noreplace) %{_sysconfdir}/sysconfig/%{name}
%attr(0755,mail,mail) %dir %{_localstatedir}/run/quotad


%changelog
* Tue Feb 08 2010 Igor Popov <ipopovi@gmail.com>
- Small fix. 
- Added rpm spec file.

* Fri Jun 18 2008 Igor Popov <ipopovi@gmail.com> - v0.2
- Both unix and tcp sockets now are supported, version is bumped to 0.2.

* Thu Jun 12 2008 Igor Popov <ipopovi@gmail.com> - v0.1
-The first working perl version 0.1 of mailquotad is released.
