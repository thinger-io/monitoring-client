# Changelog
All notable changes to this project will be documented in this file.

## [Unreleased]

## [0.0.5] - 2022-03-15
### Added
- Backup and restore available for influxdb 2.1

### Fix
- Segmentation fault when checking internal interface IP

## [0.0.4] - 2022-03-14
### Added
- New configuration options for backup and restore operations
- Added S3 upload and retrieval capabilities for backups
- Installer now allows installation of a specific version
- New distro release upgrade resource
- Check on token if device exists and change credentials of the device

### Changed
- Moved backup and restore scritps to thinger resource
- Superseded base64 class with new one
- Update and reboot resources only available when executed with root
- Service restarts by its own if it crashes
- Upload to S3 of backups is done by multipart upload instead of loading full file in memory

## [0.0.3] - 2022-01-03
### Added
- Backups and restore scripts with S3 as storage backend
- Device name in configuration file
- Kernel version as metric

### Changed
- Device name is set to hostname by default

## [0.0.2] - 2021-11-30
### Added
- Uptime as system information metric

### Changed
- Installer takes into account remote updates

### Fix
- System updates not showing
- Command execution input resource request body as JSON
- Declared input and output for update and restart resources

## [0.0.1] - 2021-11-25
### Added
- System information metrics
- CPU usage and load metrics
- RAM and swap capacity metrics
- Network public and private IPs, speeds and total transfer
- Mount points capacity metrics
- I/O drives speed and usage metrics
- Module configuration
- Execution of commands

[Unreleased]: https://github.com/thinger-io/monitoring-client/0.0.5...HEAD
[0.0.5]: https://github.com/thinger-io/monitoring-client/compare/0.0.4...0.0.5
[0.0.4]: https://github.com/thinger-io/monitoring-client/compare/0.0.3...0.0.4
[0.0.3]: https://github.com/thinger-io/monitoring-client/compare/0.0.2...0.0.3
[0.0.2]: https://github.com/thinger-io/monitoring-client/compare/0.0.1...0.0.2
[0.0.1]: https://github.com/thinger-io/monitoring-client/tag/0.0.1
