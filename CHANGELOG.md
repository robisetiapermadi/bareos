All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and since Bareos version 20 this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed
- fix invalid file descriptor issue in the libcloud plugin [PR #702]
- fix crash when loading both python-fd and python3-fd plugins [PR #730]
- fix parallel python plugin jobs [PR #729]
- fix oVirt plugin problem with config file [PR #729]
- [Issue #1316]: storage daemon loses a configured device instance [PR #739]
- fix python-bareos for Python < 2.7.13 [PR #748]
- fixed bug when user could enter wrong dates such as 2000-66-100 55:55:89 without being denied [PR #707]


### Added
- added reload commands to systemd service [PR #694]
- Build the package **bareos-filedaemon-postgresql-python-plugin** also for Debian, Ubuntu and UCS (deb packages) [PR #723].
- added an informative debugmessage when a dynamic backend cannot be loaded [PR #740]
- support for shorter date formats, where shorter dates are compensated with lowest value possible to make a full date [PR #707]

### Changed
- Disabled test "statefile" for big endian, use temporary state files for all other architectures [PR #757]
- Fixed broken link in https://docs.bareos.org/IntroductionAndTutorial/WhatIsBareos.html documentation page
- Package **bareos-database-postgresql**: add recommendation for package **dbconfig-pgsql**.
- Adapt the init scripts for some platform to not refer to a specific (outdated) configuration file, but to use the default config file instead.
- scripts: cleaned up code for postgresql db creation [PR #709]
- Change Copy Job behaviour regarding Archive Jobs [PR #717]
- py2lug-fd-ovirt systemtest: use ovirt-plugin.ini config file [PR #729]
- Ctest now runs in scripted mode. [PR #742]

### Deprecated

### Removed
- Removed outdated configuration files (example files).
- Removed package **bareos-devel**.
- Removed package **bareos-regress** and **bareos-regress-config**. The package **bareos-regress** has not been build for a long time.
- Removed unused script **breload**.
- Removed some workaround for Ubuntu 8.04.
- Removed outdated support for platforms Alpha, BSDi, Gentoo, Irix and Mandrake.
- Removed language support files for the core daemons, as these files are outdated and not used at all.
- Removed package lists for platforms no longer built: Fedora_30.x86_64, RHEL_6.x86_64, SLE_12_SP4.x86_64, openSUSE_Leap_15.0.x86_64, openSUSE_Leap_15.1.x86_64.

### Security

[unreleased]: https://github.com/bareos/bareos/tree/master