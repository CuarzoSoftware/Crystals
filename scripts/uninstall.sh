#!/bin/bash

if [[ ! $EUID -eq 0 ]]; then
    echo -e "\e[31mERROR:\e[0m Please run the script as root."
    exit 1
fi

if ! test -e CONFIG; then
    echo -e "\e[31mERROR:\e[0m Please run the script from the same directory."
    exit 1
fi

source CONFIG

rm /usr/bin/$PROJECT_NAME
rm /etc/dbus-1/system.d/com.cuarzo.$PROJECT_NAME.conf

echo -e "\e[1m\e[32mSUCCESS:\e[0m \e[1m$PROJECT_NAME\e[0m uninstalled successfully."
