#!/usr/bin/env bash
# Removes STOMPBOX Controller from the Pi.
#   sudo /opt/stompbox/uninstall.sh            keep settings in /etc/stompbox
#   sudo /opt/stompbox/uninstall.sh --purge    also delete settings and the pedal pairing
set -u
[ "$(id -u)" = 0 ] || { echo "run as root: sudo $0"; exit 1; }
systemctl disable --now stompbox-ctrl.service stompbox-ap.service 2>/dev/null
/usr/local/bin/stompbox-ap.sh stop 2>/dev/null
rm -f /etc/systemd/system/stompbox-ctrl.service /etc/systemd/system/stompbox-ap.service
rm -f /usr/local/bin/stomp /usr/local/bin/stompbox-ap.sh /usr/local/bin/stompbox-update
rm -f /etc/NetworkManager/conf.d/stompbox-unmanaged-uap0.conf
nmcli general reload conf 2>/dev/null
systemctl daemon-reload
rm -rf /opt/stompbox /opt/stompbox-src
if [ "${1:-}" = "--purge" ]; then
  rm -rf /etc/stompbox
  echo "STOMPBOX Controller removed, including settings."
else
  echo "STOMPBOX Controller removed. Settings kept in /etc/stompbox (use --purge to delete)."
fi
echo "Packages (hostapd, iw, ...) were left installed. The stock hostapd service stays masked:"
echo "  sudo systemctl unmask hostapd   if you want to use it yourself."
