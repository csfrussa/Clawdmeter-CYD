#!/bin/bash
# Claude Usage Tracker Daemon — MQTT transport (ESP32 CYD port)
#
# Reads Claude Code OAuth token, polls Anthropic API usage headers,
# and publishes a JSON payload to an MQTT broker.  The ESP32 CYD
# subscribes to the same broker and renders the data on its ILI9341 display.
#
# Dependencies: curl, mosquitto-clients
#   Linux:  sudo apt install mosquitto-clients
#   macOS:  brew install mosquitto
#
# Configuration (env vars, all optional):
#   MQTT_BROKER   — broker hostname or IP  (default: localhost)
#   MQTT_PORT     — broker port            (default: 1883)
#   POLL_INTERVAL — seconds between polls  (default: 60)

MQTT_BROKER="${MQTT_BROKER:-localhost}"
MQTT_PORT="${MQTT_PORT:-1883}"
TOPIC_USAGE="clawdmeter/usage"
TOPIC_REQUEST="clawdmeter/request"
POLL_INTERVAL="${POLL_INTERVAL:-60}"
TICK=5
REFRESH_FLAG="/tmp/claude-usage-mqtt-refresh-$$"
SUB_PID=""

log() { echo "[$(date '+%H:%M:%S')] $1"; }

read_token() {
    grep -o '"accessToken":"[^"]*"' "$HOME/.claude/.credentials.json" \
        | cut -d'"' -f4
}

poll_and_publish() {
    local token
    token=$(read_token) || { log "Error: could not read token"; return 1; }
    local now
    now=$(date +%s)

    local headers
    headers=$(curl -s -D - -o /dev/null \
        "https://api.anthropic.com/v1/messages" \
        -H "Authorization: Bearer $token" \
        -H "anthropic-version: 2023-06-01" \
        -H "anthropic-beta: oauth-2025-04-20" \
        -H "Content-Type: application/json" \
        -H "User-Agent: claude-code/2.1.5" \
        -d '{"model":"claude-haiku-4-5-20251001","max_tokens":1,"messages":[{"role":"user","content":"hi"}]}' \
        2>/dev/null) || { log "Error: API call failed"; return 1; }

    local s5h_util s5h_reset s7d_util s7d_reset status
    s5h_util=$(echo "$headers" | grep -i "anthropic-ratelimit-unified-5h-utilization"  | tr -d '\r' | awk '{print $2}')
    s5h_reset=$(echo "$headers" | grep -i "anthropic-ratelimit-unified-5h-reset"        | tr -d '\r' | awk '{print $2}')
    s7d_util=$(echo "$headers" | grep -i "anthropic-ratelimit-unified-7d-utilization"  | tr -d '\r' | awk '{print $2}')
    s7d_reset=$(echo "$headers" | grep -i "anthropic-ratelimit-unified-7d-reset"        | tr -d '\r' | awk '{print $2}')
    status=$(echo "$headers"   | grep -i "anthropic-ratelimit-unified-5h-status"       | tr -d '\r' | awk '{print $2}')

    s5h_util=${s5h_util:-0}; s5h_reset=${s5h_reset:-0}
    s7d_util=${s7d_util:-0}; s7d_reset=${s7d_reset:-0}
    status=${status:-unknown}

    local payload
    payload=$(awk \
        -v u5="$s5h_util" -v r5="$s5h_reset" \
        -v u7="$s7d_util" -v r7="$s7d_reset" \
        -v st="$status"   -v now="$now" \
        'BEGIN {
            sp = sprintf("%.0f", u5 * 100);
            sr = (r5 - now) / 60; sr = sr > 0 ? sprintf("%.0f", sr) : 0;
            wp = sprintf("%.0f", u7 * 100);
            wr = (r7 - now) / 60; wr = wr > 0 ? sprintf("%.0f", wr) : 0;
            printf "{\"s\":%s,\"sr\":%s,\"w\":%s,\"wr\":%s,\"st\":\"%s\",\"ok\":true}",
                   sp, sr, wp, wr, st;
        }')

    log "Publishing: $payload"
    mosquitto_pub \
        -h "$MQTT_BROKER" -p "$MQTT_PORT" \
        -t "$TOPIC_USAGE" -m "$payload" --retain \
        || { log "MQTT publish failed"; return 1; }
    return 0
}

# Subscribe to refresh requests from the device.
# The ESP32 publishes "refresh" to TOPIC_REQUEST after reconnect; we drop a
# flag file that the main loop picks up on its next tick.
start_refresh_subscriber() {
    mosquitto_sub \
        -h "$MQTT_BROKER" -p "$MQTT_PORT" \
        -t "$TOPIC_REQUEST" 2>/dev/null | \
    while read -r _msg; do
        log "Refresh requested by device"
        touch "$REFRESH_FLAG"
    done &
    SUB_PID=$!
    log "Refresh subscriber started (pid=$SUB_PID)"
}

stop_refresh_subscriber() {
    [ -n "$SUB_PID" ] && kill "$SUB_PID" 2>/dev/null
    SUB_PID=""
    rm -f "$REFRESH_FLAG"
}

cleanup() {
    stop_refresh_subscriber
    log "Daemon stopped"
    exit 0
}
trap cleanup INT TERM

log "=== Claude Usage Daemon (MQTT) ==="
log "Broker:        ${MQTT_BROKER}:${MQTT_PORT}"
log "Poll interval: ${POLL_INTERVAL}s"

if ! command -v mosquitto_pub &>/dev/null; then
    log "Error: mosquitto_pub not found."
    log "  Linux:  sudo apt install mosquitto-clients"
    log "  macOS:  brew install mosquitto"
    exit 1
fi

start_refresh_subscriber

# Initial poll on startup
poll_and_publish
LAST_POLL=$(date +%s)

while true; do
    NOW=$(date +%s)
    if [ -f "$REFRESH_FLAG" ] || (( NOW - LAST_POLL >= POLL_INTERVAL )); then
        [ -f "$REFRESH_FLAG" ] && rm -f "$REFRESH_FLAG"
        poll_and_publish && LAST_POLL=$NOW
    fi
    sleep "$TICK"
done
