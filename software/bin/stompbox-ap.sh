#!/bin/bash
# stompbox-ap.sh – the Pi's own Wi-Fi hotspot (default STOMPBOX-CTRL, 192.168.4.1).
#
# Runs hostapd on a virtual interface (uap0) next to the normal Wi-Fi (wlan0,
# managed by NetworkManager), plus dnsmasq for addresses/DNS and nftables rules
# that turn the hotspot into a captive portal for the settings page.
#
# The Pi Zero W has ONE radio, so the hotspot must use the same channel as the
# network wlan0 is on. With no network in range it uses channel 6.
#
# Usage: stompbox-ap.sh          start (or restart) the hotspot
#        stompbox-ap.sh stop     stop it and remove uap0
# Settings: /etc/stompbox/hotspot.conf  (SSID, PASSWORD, COUNTRY)

ETC=/etc/stompbox
RUN=/run/stompbox
mkdir -p $RUN

SSID="STOMPBOX-CTRL"; PASSWORD="stompbox123"; COUNTRY="IE"
# shellcheck disable=SC1091
[ -f $ETC/hotspot.conf ] && . $ETC/hotspot.conf

home_up() { nmcli -t -f DEVICE,STATE dev | grep -q "^wlan0:connected"; }

stop_all() {
  nft delete table ip stompbox_captive 2>/dev/null
  [ -f $RUN/hostapd.pid ] && kill "$(cat $RUN/hostapd.pid)" 2>/dev/null
  [ -f $RUN/dnsmasq.pid ] && kill "$(cat $RUN/dnsmasq.pid)" 2>/dev/null
  rm -f $RUN/*.pid
  sleep 1
}

if [ "$1" = stop ]; then
  stop_all; iw dev uap0 del 2>/dev/null; exit 0
fi
stop_all

iw dev | grep -q "Interface uap0" || iw dev wlan0 interface add uap0 type __ap || exit 1

ch=""
for i in $(seq 1 15); do
  ch=$(iw dev wlan0 info 2>/dev/null | awk '/channel/{print $2; exit}')
  [ -n "$ch" ] && break
  sleep 2
done
ch=${ch:-6}
had_home=no; home_up && had_home=yes
echo "hotspot $SSID, home Wi-Fi connected: $had_home, channel: $ch"

{
  echo "interface=uap0"
  echo "driver=nl80211"
  echo "ssid=$SSID"
  echo "country_code=$COUNTRY"
  echo "hw_mode=g"
  echo "channel=$ch"
  echo "ieee80211n=1"
  echo "wmm_enabled=1"
  echo "auth_algs=1"
  echo "ignore_broadcast_ssid=0"
  if [ -n "$PASSWORD" ]; then
    echo "wpa=2"
    echo "wpa_key_mgmt=WPA-PSK"
    echo "rsn_pairwise=CCMP"
    echo "wpa_passphrase=$PASSWORD"
  fi
} > $RUN/hostapd.conf
chmod 600 $RUN/hostapd.conf

ip link set uap0 down
ip addr flush dev uap0
ip addr add 192.168.4.1/24 dev uap0

ok=no
for attempt in 1 2 3; do
  if hostapd -B -P $RUN/hostapd.pid $RUN/hostapd.conf; then ok=yes; break; fi
  echo "hostapd failed (attempt $attempt), retrying"; sleep 5
done
[ "$ok" = yes ] || exit 1
sleep 2
ip link set uap0 up
dnsmasq -C $ETC/dnsmasq.conf --pid-file=$RUN/dnsmasq.pid || echo "dnsmasq failed"
nft -f $ETC/captive.nft || echo "captive portal rules failed"

# Safety net: if the Pi was on a network before and loses it because of the
# hotspot, remove the hotspot so the Pi stays reachable on that network.
if [ "$had_home" = yes ]; then
  (
    for i in $(seq 1 12); do
      sleep 10
      home_up && continue
      sleep 20
      home_up && continue
      echo "network lost after hotspot start - removing hotspot"
      stop_all; iw dev uap0 del; nmcli dev connect wlan0
      exit 0
    done
  ) &
fi
exit 0
