#!/bin/bash

trap 'echo "Caught: 90 seconds till SIGKILL' SIGTERM # catches systemctl stop on update

_repo="monitoring-client"
_module="thinger_monitor"
_github_api_url="api.github.com"
_user_agent="Simple User Agent 1.0"

# archtitecture to download
case `uname -m` in
  x86_64)
    _arch=`uname -m` ;;
  amd64)
    _arch="x86_64" ;;
  *arm*)
    _arch="arm" ;;
  *aarch64*)
    _arch="arm" ;;
esac

usage() {
    echo "usage: install_$_module.sh [-h] [-t TOKEN] [-s SERVER] [-k] [-u]"
    echo
    echo "Installs and runs $_module binary"
    echo
    echo "optional arguments:"
    echo " -h, --help            show this help message and exit"
    echo " -t TOKEN, --token TOKEN"
    echo "                       Thinger.io Platform Access Token with create device permissions"
    echo " -s SERVER, --server SERVER"
    echo "                       Ip or URL where the Thinger.io Platform is installed"
    echo " -v, --version         Version of monitoring-client to install. Default is latest published production release"
    echo " -k, --insecure        To not verify SSL certificates, specially for local deployments"
    echo " -u, --uninstall       Uninstalls the $_module program and removes all associated files"

    exit 0
}

set_directories() {
    # Set install directories based on user
    if [ "$UID" -eq 0 ]; then
        export bin_dir="/usr/local/bin/"
        export config_dir="/etc/thinger_io/"
        export home_dir="$HOME"
        service_dir="/etc/systemd/system/"
        sys_user=""
    else
        export bin_dir="$HOME/.local/bin/"
        export config_dir="$HOME/.config/thinger_io"
        export home_dir="$HOME"
        service_dir="$HOME/.config/systemd/user/"
        sys_user="--user"

  fi
}

uninstall() {
    # remove bin, disable and remove service, remove config
    #echo "Uninstalling $_module"

    set_directories

    rm -f "$bin_dir"/"$_module"

    systemctl $sys_user stop "$_module"
    systemctl $sys_user disable "$_module"
    rm -f "$service_dir"/"$_module".service

    rm -f "$config_dir"/"$_module".json

    echo "Uninstalled thinger_monitor"
    exit 0
}

# Parse options
while [[ "$#" -gt 0 ]]; do case $1 in
  -t | --token )
    shift; token="-t $1"
    ;;
  -s | --server )
    shift; server="-s $1"
    ;;
  -v | --version )
    shift; version="$1"
    ;;
  -k | --insecure )
    shift; insecure="-k"
    ;;
  -u | --uninstall )
    uninstall
    ;;
  -h | --help )
    usage
    ;;
  *)
    usage
    ;;
esac; shift; done

set_directories
mkdir -p $bin_dir $config_dir $service_dir

# Set SSL_CERT_DIR if exists
if [ -n "${SSL_CERT_DIR+x}" ]; then
    export certs_dir_env="Environment=SSL_CERT_DIR=$SSL_CERT_DIR"
fi

if [ -z "${version+x}" ]; then
  version="`wget --quiet -qO- --header="Accept: application/vnd.github.v3+json" https://"$_github_api_url"/repos/thinger-io/"$_repo"/releases/latest | grep "tag_name" | cut -d '"' -f4`"
fi

# Download service file -> Before downloading binary
if [ -f "$service_dir"/"$_module".service ]; then
    systemctl $sys_user stop "$_module".service
    systemctl $sys_user disable "$_module".service
fi
wget -q --header="Accept: application/vnd.github.VERSION.raw" https://"$_github_api_url"/repos/thinger-io/"$_repo"/contents/install/"$_module".template?ref="$version" -P "$service_dir" -O "$service_dir"/"$_module".template
cat "$service_dir"/"$_module".template | envsubst '$home_dir,$certs_dir_env,$bin_dir,$config_dir' > "$service_dir"/"$_module".service
rm -f "$service_dir"/"$_module".template

# Download bin
version_release_body=`wget --quiet -qO- --header="Accept: application/vnd.github.v3+json" https://"$_github_api_url"/repos/thinger-io/"$_repo"/releases/tags/"$version"`
download_url=`echo "$version_release_body" | grep "url.*$_arch" | cut -d '"' -f4`

wget -q --header="Accept: application/octec-stream" "$download_url" -O "$bin_dir/$_module"
chmod +x "$bin_dir"/"$_module"

# Download config
if [ ! -f "$config_dir"/thinger_monitor.json ]; then
  wget -q --header="Accept: application/vnd.github.VERSION.raw" https://"$_github_api_url"/repos/thinger-io/"$_repo"/contents/config/thinger_monitor.json?ref="$version" -P "$config_dir" -O "$config_dir"/thinger_monitor.json
fi

# First run with token for autoprovision
if [ -n "$token" ]; then
  "$bin_dir"/thinger_monitor -c "$config_dir"/"$_module".json $token $server $insecure
fi

# Start and enable service
systemctl $sys_user daemon-reload
systemctl $sys_user enable "$_module".service
systemctl $sys_user start "$_module".service

exit 0
