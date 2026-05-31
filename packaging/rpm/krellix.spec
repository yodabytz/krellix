Name:           krellix
Version:        0.1.2
Release:        1%{?dist}
Summary:        Themeable Qt 6 system monitor in the spirit of GKrellM
License:        GPL-3.0-or-later
URL:            https://github.com/yodabytz/krellix
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.21
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qttools-devel

Requires:       qt6-qtbase

%description
Krellix is a themeable desktop system monitor inspired by GKrellM.
It displays CPU, memory, disk, network, temperature sensors, uptime,
and more, with a plugin architecture for extensibility.

Requires a running X11 or Wayland session.

%package server
Summary:        krellixd — remote monitoring daemon for krellix
Requires:       qt6-qtbase
%description server
The krellixd daemon exposes system metrics over TCP so that krellix can
monitor remote hosts. Run it on the host you want to watch and point
krellix at it with --host.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=%{_prefix}
%cmake_build

%install
%cmake_install
rm -f %{buildroot}%{_bindir}/krellixd
rm -rf %{buildroot}%{_libdir}/krellix/plugins
rm -rf %{buildroot}%{_datadir}/krellix/plugins

install -Dm755 %{__cmake_builddir}/bin/krellixd \
    %{buildroot}%{_bindir}/krellixd

install -Dm644 packaging/systemd/krellixd.service \
    %{buildroot}%{_unitdir}/krellixd.service

install -dm755 %{buildroot}%{_sysconfdir}/krellixd
cat > %{buildroot}%{_sysconfdir}/krellixd/krellixd.conf <<'CONF'
[server]
listen=127.0.0.1
port=19150
update_hz=1
max_clients=4
io_timeout=300

[security]
allow_hosts=
CONF

%files
%license LICENSE
%doc README.md
%{_bindir}/krellix
%{_datadir}/krellix/

%files server
%license LICENSE
%{_bindir}/krellixd
%{_unitdir}/krellixd.service
%config(noreplace) %{_sysconfdir}/krellixd/krellixd.conf

%changelog
* Sat May 31 2026 yodabytz <hello@cerberix.org> - 0.1.2-1
- Thermal sensor display: color-coded green/warn/red, F/C toggle,
  display mode (Temperature + %, Temperature only, % only),
  configurable warn/critical thresholds
- hardware tempN_max/tempN_crit thresholds read from /sys/class/hwmon
