#!/usr/bin/env bash
# =============================================================================
#  STOMPBOX Controller – installer for Raspberry Pi Zero W / Zero 2 W
#  https://github.com/Pablokaer/stompboxcontroller
#
#  Fresh Pi (one line, over SSH):
#    curl -fsSL https://raw.githubusercontent.com/Pablokaer/stompboxcontroller/main/install.sh | sudo bash
#
#  From a checkout:      sudo ./install.sh
#  Update later:         sudo stompbox-update
#
#  Safe to run again: it only installs what's missing, updates the program
#  files, and keeps your settings (buttons, pedal, hotspot, Wi-Fi networks).
#
#  Optional settings (environment variables, first install or to change them):
#    STOMPBOX_SSID=MY-PEDAL STOMPBOX_PASSWORD=secret123 STOMPBOX_COUNTRY=IE
#    (STOMPBOX_PASSWORD="" makes an open hotspot)
#    STOMPBOX_REPO=<git url>  STOMPBOX_BRANCH=<branch>
# =============================================================================
set -euo pipefail

REPO=${STOMPBOX_REPO:-https://github.com/Pablokaer/stompboxcontroller.git}
BRANCH=${STOMPBOX_BRANCH:-main}
SRC=/opt/stompbox-src
ETC=/etc/stompbox
APP=/opt/stompbox

say()  { printf '\n\033[1;33m==> %s\033[0m\n' "$*"; }
info() { printf '    %s\n' "$*"; }
die()  { printf '\n\033[1;31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }

[ "$(id -u)" = 0 ] || die "run as root:  sudo bash install.sh"
export DEBIAN_FRONTEND=noninteractive

# ----------------------------------------------------------------- 1. source
# Use the folder this script is in if it is a checkout; otherwise (curl | bash)
# clone the repository to $SRC and continue from there.
HERE=""
if [ -n "${BASH_SOURCE[0]:-}" ] && [ -f "${BASH_SOURCE[0]}" ]; then
  HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
fi
if [ -z "$HERE" ] || [ ! -f "$HERE/software/stompboxd.py" ]; then
  say "Downloading STOMPBOX Controller ($BRANCH)"
  if ! command -v git >/dev/null; then apt-get update -q && apt-get install -y -q git; fi
  if [ -d $SRC/.git ]; then
    git -C $SRC fetch --depth 1 origin "$BRANCH" && git -C $SRC reset --hard FETCH_HEAD
  else
    rm -rf $SRC && git clone --depth 1 -b "$BRANCH" "$REPO" $SRC
  fi
  exec bash $SRC/install.sh
fi
S=$HERE/software
VERSION=$(sed -n 's/^VERSION = "\(.*\)"/\1/p' $S/stompboxd.py)
say "Installing STOMPBOX Controller $VERSION from $HERE"

# --------------------------------------------------------------- 2. packages
PKGS="python3 python3-lgpio bluez alsa-utils network-manager iw hostapd dnsmasq-base nftables git curl"
MISSING=""
for p in $PKGS; do
  dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q "install ok installed" || MISSING="$MISSING $p"
done
if [ -n "$MISSING" ]; then
  say "Installing packages:$MISSING   (a few minutes on a Pi Zero)"
  apt-get update -q
  apt-get install -y -q --no-install-recommends $MISSING
else
  info "all packages already installed"
fi
command -v aseqsend >/dev/null || die "aseqsend is missing: use Raspberry Pi OS based on Debian 13 'Trixie' (alsa-utils 1.2.11 or newer)"
systemctl is-active --quiet NetworkManager || die "NetworkManager is not running: use a current Raspberry Pi OS (Lite) image"

# hostapd is driven by our own hotspot service, not the stock one
systemctl disable --now hostapd >/dev/null 2>&1 || true
systemctl mask hostapd >/dev/null 2>&1 || true

# ----------------------------------------------------- 3. older setups (v0.x)
if [ -f /etc/systemd/system/stompbox-autoconnect.service ] || [ -f /usr/local/bin/stompbox-autoconnect.sh ]; then
  say "Migrating from the older hand-made setup"
  if [ ! -f $ETC/pedal.json ] && [ -f /usr/local/bin/stompbox-autoconnect.sh ]; then
    OLDMAC=$(sed -n 's/^MAC="\([0-9A-F:]\{17\}\)".*/\1/p' /usr/local/bin/stompbox-autoconnect.sh | head -1)
    if [ -n "$OLDMAC" ]; then
      mkdir -p $ETC
      printf '{"mac": "%s", "name": "JME STOMPBOX", "paired": "migrated"}\n' "$OLDMAC" > $ETC/pedal.json
      info "kept the paired pedal $OLDMAC"
    fi
  fi
  systemctl disable --now stompbox-autoconnect.service >/dev/null 2>&1 || true
  rm -f /etc/systemd/system/stompbox-autoconnect.service /usr/local/bin/stompbox-autoconnect.sh
fi
if nmcli -t -f NAME con show 2>/dev/null | grep -qx "STOMPBOX-CTRL"; then
  nmcli con delete STOMPBOX-CTRL >/dev/null 2>&1 || true      # old NetworkManager hotspot
fi
rm -f /etc/NetworkManager/dnsmasq-shared.d/stompbox.conf $ETC/hostapd.conf   # replaced by hotspot.conf

# ------------------------------------------------------------------ 4. files
say "Installing program files"
mkdir -p $APP $ETC
rm -rf $APP/web
cp -r $S/web $APP/web
install -m 755 $S/stompboxd.py $APP/stompboxd.py
for f in stomp stompbox-ap.sh stompbox-update; do
  rm -f /usr/local/bin/$f                     # may be an old symlink
  install -m 755 $S/bin/$f /usr/local/bin/$f
done
install -m 644 $S/system/stompbox-ctrl.service $S/system/stompbox-ap.service /etc/systemd/system/
install -m 644 $S/system/dnsmasq.conf $S/system/captive.nft $ETC/
install -m 644 $S/system/nm-unmanaged-uap0.conf /etc/NetworkManager/conf.d/stompbox-unmanaged-uap0.conf
cp $HERE/uninstall.sh $APP/uninstall.sh && chmod 755 $APP/uninstall.sh
git -C $HERE rev-parse --short HEAD > $APP/commit 2>/dev/null || echo "local" > $APP/commit
echo "$VERSION" > $APP/version

# -------------------------------------------------------- 5. hotspot settings
CONF=$ETC/hotspot.conf
SSID="STOMPBOX-CTRL"; PASSWORD="stompbox123"
COUNTRY=$(iw reg get 2>/dev/null | awk '/^country/{sub(":","",$2); print $2; exit}')
if [ -z "$COUNTRY" ] || [ "$COUNTRY" = "00" ]; then COUNTRY="IE"; fi
# shellcheck disable=SC1090
[ -f $CONF ] && . $CONF
SSID=${STOMPBOX_SSID:-$SSID}
PASSWORD=${STOMPBOX_PASSWORD-$PASSWORD}
COUNTRY=${STOMPBOX_COUNTRY:-$COUNTRY}
[ ${#SSID} -ge 1 ] && [ ${#SSID} -le 32 ] || die "hotspot name must be 1-32 characters"
[ -z "$PASSWORD" ] || { [ ${#PASSWORD} -ge 8 ] && [ ${#PASSWORD} -le 63 ]; } || die "hotspot password must be 8-63 characters (or empty for an open network)"
{
  echo "# STOMPBOX hotspot – change these, then: sudo systemctl restart stompbox-ap"
  printf 'SSID=%q\n' "$SSID"
  printf 'PASSWORD=%q\n' "$PASSWORD"
  printf 'COUNTRY=%q\n' "$COUNTRY"
} > $CONF
chmod 600 $CONF
info "hotspot: $SSID  (password: ${PASSWORD:-none, open network})  country: $COUNTRY"

# --------------------------------------------------------------- 6. services
say "Starting services"
nmcli general reload conf >/dev/null 2>&1 || systemctl reload NetworkManager || true
systemctl daemon-reload
systemctl enable stompbox-ctrl.service stompbox-ap.service >/dev/null 2>&1
systemctl restart stompbox-ctrl.service
systemctl restart stompbox-ap.service || info "hotspot did not start yet – check: journalctl -u stompbox-ap"

# ------------------------------------------------------------------ 7. done
sleep 3
IP=$(nmcli -g IP4.ADDRESS dev show wlan0 2>/dev/null | head -1 | cut -d/ -f1)
say "Done – STOMPBOX Controller $VERSION is running"
info "Hotspot:   $SSID   ->  http://192.168.4.1"
[ -n "$IP" ] && info "This network: http://$IP"
if [ -f $ETC/pedal.json ]; then
  info "Pedal:     $(sed -n 's/.*"mac": "\([^"]*\)".*/\1/p' $ETC/pedal.json) (already paired)"
else
  info "Pedal:     switch the JME STOMPBOX on – it pairs automatically within a minute"
fi
info "Status:    stomp status        Logs: journalctl -u stompbox-ctrl -f"
