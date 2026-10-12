#!/bin/sh
# nd100x-net.sh start|stop - put the ND (Ethernet II card on TAP nd0) on the
# host's LAN by proxy ARP + routing. Nothing on eth0/wlan0 is reconfigured
# (no bridge, no address change), so it is safe over ssh and works on Wi-Fi.
# Settings come from /etc/default/nd100x (see nd100x.default).
set -e
[ -r /etc/default/nd100x ] && . /etc/default/nd100x
ND_IP=${ND_IP:-192.168.1.40}
ND_UPLINK=${ND_UPLINK:-wlan0}          # wlan0 or eth0
ND_TAP=${ND_TAP:-nd0}
ND_USER=${ND_USER:-root}
TABLE=${ND_TABLE:-140}
ND_MODE=${ND_MODE:-proxyarp}           # proxyarp (wlan0 or eth0) | bridge (eth0 only)
ND_BRIDGE=${ND_BRIDGE:-br0}

if [ "$ND_MODE" = bridge ]; then
    # eth0 + nd0 in one bridge; dhcpcd must run on $ND_BRIDGE, not on eth0
    # (/etc/dhcpcd.conf: denyinterfaces eth0 nd0). Wi-Fi cannot be bridged.
    case "$1" in
    start)
        ip tuntap add dev "$ND_TAP" mode tap user "$ND_USER" 2>/dev/null || true
        ip link show "$ND_BRIDGE" >/dev/null 2>&1 || ip link add "$ND_BRIDGE" type bridge
        ip link set "$ND_BRIDGE" address "$(cat /sys/class/net/eth0/address)"
        ip link set eth0 master "$ND_BRIDGE"
        ip link set "$ND_TAP" master "$ND_BRIDGE"
        ip link set "$ND_TAP" up; ip link set eth0 up; ip link set "$ND_BRIDGE" up
        systemctl restart dhcpcd
        ;;
    stop)
        ip link set eth0 nomaster 2>/dev/null || true
        ip link del "$ND_BRIDGE" 2>/dev/null || true
        ip tuntap del dev "$ND_TAP" mode tap 2>/dev/null || true
        systemctl restart dhcpcd
        ;;
    esac
    exit 0
fi

case "$1" in
start)
    ip link show "$ND_UPLINK" >/dev/null
    ip tuntap add dev "$ND_TAP" mode tap user "$ND_USER" 2>/dev/null || true
    ip link set "$ND_TAP" up
    ip route replace "$ND_IP/32" dev "$ND_TAP"
    sysctl -q -w net.ipv4.ip_forward=1
    sysctl -q -w "net.ipv4.conf.$ND_TAP.proxy_arp=1" "net.ipv4.conf.$ND_UPLINK.proxy_arp=1"
    sysctl -q -w "net.ipv4.conf.$ND_TAP.rp_filter=0" "net.ipv4.conf.$ND_UPLINK.rp_filter=0"
    # Everything the ND sends leaves through the chosen uplink, even when
    # the other interface holds the main default route.
    # At boot the uplink may not have its DHCP lease yet: wait (max 90 s)
    i=0
    while [ $i -lt 90 ] && ! ip -4 route show default dev "$ND_UPLINK" | grep -q via; do
        sleep 1; i=$((i+1))
    done
    GW=$(ip -4 route show default dev "$ND_UPLINK" | awk '{print $3; exit}')
    NET=$(ip -4 route show dev "$ND_UPLINK" scope link | awk '{print $1; exit}')
    ip rule del from "$ND_IP" lookup "$TABLE" 2>/dev/null || true
    ip rule add from "$ND_IP" lookup "$TABLE"
    ip route flush table "$TABLE" 2>/dev/null || true
    if [ -n "$NET" ]; then ip route add "$NET" dev "$ND_UPLINK" table "$TABLE"; fi
    if [ -n "$GW" ]; then ip route add default via "$GW" dev "$ND_UPLINK" table "$TABLE"; fi
    ;;
stop)
    ip rule del from "$ND_IP" lookup "$TABLE" 2>/dev/null || true
    ip route flush table "$TABLE" 2>/dev/null || true
    ip route del "$ND_IP/32" dev "$ND_TAP" 2>/dev/null || true
    sysctl -q -w "net.ipv4.conf.$ND_UPLINK.proxy_arp=0" 2>/dev/null || true
    ip tuntap del dev "$ND_TAP" mode tap 2>/dev/null || true
    ;;
*) echo "usage: $0 start|stop" >&2; exit 2;;
esac
