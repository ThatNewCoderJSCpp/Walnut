if [ "$1" = "0" ]; then
    if [ -L /usr/bin/walnut ] && [ "$(readlink /usr/bin/walnut)" = /opt/walnut/bin/walnut ]; then
        rm -f /usr/bin/walnut
    fi
fi
