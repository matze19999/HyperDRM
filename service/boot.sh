#!/bin/sh
# HyperDRM: container-owned startup script, linked by the app.
SERVICE_DIR=/media/developer/apps/usr/palm/services/org.webosbrew.histogramhyperion.service
TARGET="$SERVICE_DIR/histogram-service"
HOOK=/var/lib/webosbrew/init.d/60-histogramhyperion
[ -x "$TARGET" ] && [ -f "$SERVICE_DIR/services.json" ] || exit 0
command -v luna-send >/dev/null 2>&1 || exit 0
command -v timeout >/dev/null 2>&1 || exit 0
case "$0" in
  */61-hyperdrm-mqtt)
    (
      while [ -x "$TARGET" ] && [ -L "$0" ] && [ "$(readlink "$0")" = "$SERVICE_DIR/boot.sh" ]; do
        result=$(timeout 12 luna-send -n 1 luna://org.webosbrew.histogramhyperion.service/mqttStatus '{}' 2>&1)
        case "$result" in
          *'"rootRequired":true'*)
            # The MQTT hook exists only after MQTT has been enabled on this TV.
            # Repair through Homebrew's service API and wake only our service.
            [ -z "$repair_attempted" ] || exit 1
            repair_attempted=1
            elevated=$(timeout 25 luna-send -n 1 luna://org.webosbrew.hbchannel.service/elevateService '{"id":"org.webosbrew.histogramhyperion.service"}' 2>&1)
            case "$elevated" in
              *'"returnValue":true'*)
                timeout 10 luna-send -n 1 luna://org.webosbrew.histogramhyperion.service/restartService '{}' >/dev/null 2>&1
                ;;
              *) exit 1 ;;
            esac
            ;;
          *'"returnValue":true'*) exit 0 ;;
        esac
        sleep 5
      done
    ) >/dev/null 2>&1 </dev/null &
    exit 0 ;;
esac
(
  echo 'HyperDRM boot start requested'
  while [ -x "$TARGET" ] && [ -f "$SERVICE_DIR/services.json" ] && [ -L "$HOOK" ] && [ "$(readlink "$HOOK")" = "$SERVICE_DIR/boot.sh" ]; do
    result=$(timeout 12 luna-send -n 1 luna://org.webosbrew.histogramhyperion.service/start '{}' 2>&1)
    case "$result" in
      *'"running":true'*) echo 'HyperDRM service started'; exit 0 ;;
      *'"retryScheduled":true'*) echo 'HyperDRM service is retrying startup in the background'; exit 0 ;;
      *'"retryBlocked":true'*) echo 'HyperDRM startup requires manual intervention'; exit 0 ;;
    esac
    sleep 5
  done
  echo 'HyperDRM boot start cancelled, target removed'
) >/var/lib/webosbrew/histogramhyperion-boot.log 2>&1 </dev/null &
exit 0
