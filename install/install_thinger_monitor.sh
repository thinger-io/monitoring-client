#!/bin/bash

_repo="monitoring_client"
_module="thinger_monitor"
_github_api_url="api.github.com"
_user_agent="Simple User Agent 1.0"

_arch=`uname -m`

usage() {
    echo "usage: install_$_monitor.sh [-h] [-t TOKEN] [-s SERVER] [-k] [-u]"
    echo
    echo "Installs and runs $_module binary"
    echo
    echo "optional arguments:"
    echo " -h, --help            show this help message and exit"
    echo " -t TOKEN, --token TOKEN"
    echo "                       Thinger.io Platform Access Token with create device permissions"
    echo " -s SERVER, --server SERVER"
    echo "                       Ip or URL where the Thinger.io Platform is installed"
    echo " -k, --insecure        To not verify SSL certificates, specially for local deployments"
    echo " -u, --uninstall       Uninstalls the $_module program and removes all associated files"

    exit 0
}

uninstall() {
    # remove bin, disable and remove service, remove config
    echo "Uninstalling $_module"

    rm -f "$bin_dir"/"$_module"

    systemctl "$sys_user" stop "$_module"
    systemctl "$sys_user" disable "$_module"
    rm -f "$service_dir"/"$_module".service

    rm -f "$config_dir"/"$_module".json
}

# Parse options
while [[ "$#" -gt 0 ]]; do case $1 in
  -t | --token )
    shift; token="-t $1"
    ;;
  -s | --server )
    shift; server="-s $1"
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

# Set install directories based on user
if [ "$UID" -eq 0 ]; then
    bin_dir="/usr/local/bin/"
    config_dir="/etc/thinger_io/"
    service_dir="/etc/systemd/system/"
    sys_user=""
else
    bin_dir="$HOME/.local/bin/"
    config_dir="$HOME/.config/thinger_io"
    service_dir="$HOME/.local/systemd/user/"
    sys_user="--user"

    mkdir -p $bin_dir $config_dir $service_dir
fi

# Download bin
last_release_body=`wget --header="Accept: application/vnd.github.v3+json" https://"$_github_api_url"/repos/thinger-io/"$_repo"/releases/latest`
download_url=`echo "$last_realease_body" | grep "url.*$_arch" | cut -d '"' -f4`

wget --header="Accept: application/octec-stream" "$download_url" -P "$bin_dir" -O "$_module"
chmod +x "$bin_dir"/"$_module"

# Download config (is not actually neccesary)
#cd $CONFIG_DIR
#curl -s -O -u "$GITHUB_USER":"$GITHUB_TOKEN" -H "Accept: application/vnd.github.VERSION.raw" "$GITHUB_CONFIG_URL"
#cd - 1>/dev/null

# Download service file
wget --header="Accept: application/vnd.github.VERSION.raw" https://"$_github_api_url"/repos/thinger-io/"$_repo"/contents/install/"$_module".template -P "$service_dir"
envsubst < "$service_dir"/"$_module".template > "$service_dir"/"$_module".service
rm -f "$service_dir"/"$_module".template

# First run with token for autoprovision
"$bin_dir"/thinger_monitor -c "$config_dir"/"$_module".json "$token" "$server" "$insecure"

# Start and enable service
systemctl "$sys_user" enable "$_module".service
systemctl "$sys_user" start "$_module".service

exit 0
