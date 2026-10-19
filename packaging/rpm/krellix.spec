Name:           krellix
Version:        0.1.2
Release:        10%{?dist}
Summary:        Themeable Qt 6 system monitor in the spirit of GKrellM
License:        GPL-3.0-or-later
URL:            https://github.com/yodabytz/krellix
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.21
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qttools-devel
BuildRequires:  systemd-rpm-macros

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

%package krellbluetooth
Summary:        KrellBT compact Bluetooth device and battery monitor plugin
Requires:       krellix >= 0.1.2-10
Requires:       bluez
%description krellbluetooth
Optional local Bluetooth monitor using BlueZ. Disabled by default.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=%{_prefix}
%cmake_build

%check
QT_QPA_PLATFORM=offscreen ctest --test-dir %{__cmake_builddir} --output-on-failure

%install
%cmake_install
find %{buildroot}%{_prefix}/lib/krellix/plugins -type f ! -name libkrellbluetooth.so -delete
rm -rf %{buildroot}%{_datadir}/krellix/plugins


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

%files krellbluetooth
%license LICENSE
%{_prefix}/lib/krellix/plugins/libkrellbluetooth.so

%changelog
* Mon Oct 19 2026 yodabytz <hello@cerberix.org> - 0.1.2-10
- Add optional compact KrellBT plugin with theme accents and battery monitoring

* Sun Oct 18 2026 yodabytz <hello@cerberix.org> - 0.1.2-9
- Keep the monitor running when Settings closes; test dialog lifecycle

* Tue Jun 09 2026 yodabytz <hello@cerberix.org> - 0.1.2-8
- macOS: force INI settings so ~/.config/krellix/krellix.ini takes precedence
- macOS: grouped CPU/GPU/I/O sensor temperatures with percent pressure
- KrellStock: rotate through every configured symbol and show No data rows
- Add default accent colors for visible green/yellow/red sensor states

* Sun May 31 2026 yodabytz <hello@cerberix.org> - 0.1.2-1
- Thermal sensor display: color-coded green/warn/red, F/C toggle,
  display mode (Temperature + %, Temperature only, % only),
  configurable warn/critical thresholds
- hardware tempN_max/tempN_crit thresholds read from /sys/class/hwmon
