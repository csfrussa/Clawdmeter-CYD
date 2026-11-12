# Contexto do projeto — Clawdmeter-CYD

Firmware para um medidor de uso do Claude Code numa **ESP32 "Cheap Yellow Display" (CYD)**: ESP32 clássico + ILI9341 320×240 + touch resistivo XPT2046. Recebe os dados por **Wi-Fi + MQTT**. Um daemon no n8n-srv consulta a API da Anthropic e publica no broker.

Este repo é um **fork público** (`csfrussa/Clawdmeter-CYD`) do `HermannBjorgvin/Clawdmeter`. O upstream é outra placa (Waveshare ESP32-S3 AMOLED 2.16, BLE). Vários arquivos herdados não valem para o CYD (ver "Legado do upstream").

Nota de status no vault: `HomeServer/ia-local/clawdmeter-cyd.md`. O vault guarda o status; este arquivo guarda os mecanismos.

## Regra de segredos (repo público)

- **Nunca** colocar usuário ou senha MQTT em arquivo rastreado. A unit lê de `~/.config/claude-usage-monitor/mqtt.env` (`EnvironmentFile`, `chmod 600`, fora do repo). O modelo é `daemon/mqtt.env.example`.
- Histórico: o commit `6c54646` (2026-05-16) publicou a credencial MQTT. Ela foi invalidada em 2026-11-11: o Mosquitto do HA foi reinstalado, gerando senhas internas novas, e cada cliente ganhou login próprio (o daemon e a tela usam `clawdmeter`).
- No firmware, a credencial MQTT entra pelo portal WiFiManager e fica gravada na NVS. Os `-DMQTT_DEFAULT_*` existem, mas não devem receber valores reais num build commitado.

## Hardware (pinos em `firmware/src/display_cfg.h`)

- Display: **ILI9341** em HSPI (CS 15, DC 2, MOSI 13, SCLK 14, sem RST, sem MISO), backlight GPIO21 (ativo HIGH). Paisagem (`rotation=1`), 320×240.
- Touch: **XPT2046** em VSPI separado (CS 33, CLK 25, MOSI 32, MISO 39, IRQ 36). A calibração fica nos `TOUCH_*_MIN/MAX`, com os eixos trocados e espelhados em `touch_read()` (`main.cpp`).
- Botão do meio = GPIO0 (BOOT), lido em `power.cpp`. Toque curto: troca de tela (no splash, troca a animação). Segurar 5 s: abre o portal WiFiManager.
- `BTN_LEFT` 35 e `BTN_RIGHT` 34: só têm `pinMode`. Space e Shift+Tab eram teclado BLE no upstream e **não estão implementados** no CYD.
- LED RGB 4/16/17 (ativo LOW), mantido apagado.
- Não tem IMU, PMU nem bateria: `imu.cpp`, `power_battery_pct()` e `ble.cpp` são stubs.

## Arquitetura (`firmware/src/`)

```text
main.cpp        — setup/loop, touch_read() centralizado, flush LVGL, parse do JSON MQTT
display_cfg.h   — pinos, calibração do touch, objetos globais (gfx, touch)
wifi_mqtt.cpp/h — WiFiManager (AP "Clawdmeter-Setup", timeout 180 s) + PubSubClient; config na NVS "clawdmeter"
ui.cpp/h        — telas SCREEN_SPLASH, SCREEN_USAGE, SCREEN_WIFI
splash.cpp/h    — animações pixel-art (splash_animations.h é gerado; não editar à mão)
usage_rate.cpp  — taxa de consumo da sessão → escolhe a animação do splash
power.cpp       — botão do meio (GPIO0); bateria fixa em 100 %
ble.cpp, imu.cpp — stubs (BLE desligado: falta DRAM junto com o Wi-Fi)
font_*.c, icons.h, logo.h, theme.h — assets LVGL 9
```

O boot mostra uma tela GFX crua ("Connecting to WiFi..."), que pode bloquear no portal. Depois disso o LVGL sobe e a tela inicial é `SCREEN_SPLASH`.

## MQTT

- Broker: o padrão compilado é `192.168.1.100`, mas o valor real fica na NVS. O broker em uso é o Mosquitto do HA, `192.168.0.40:1883`.
- Tópicos: o daemon publica em `clawdmeter/usage` (retain) o JSON `{"s","sr","w","wr","st","ok"}`: % e minutos até o reset da sessão (5 h) e da semana (7 d), e o status. Ao conectar, o device publica `refresh` em `clawdmeter/request`, e o daemon responde com um poll fora de hora.
- Client id: `ClawdmeterCYD`.

## Daemon (host n8n-srv)

- `daemon/claude-usage-mqtt.sh`: lê `~/.claude/.credentials.json`, renova o OAuth quando faltam menos de 5 min para expirar, faz uma chamada mínima a `/v1/messages` e lê os headers `anthropic-ratelimit-unified-*`. Publica a cada 60 s (`POLL_INTERVAL`) com `mosquitto_pub` (pacote `mosquitto-clients`).
- Unit: `daemon/claude-usage-mqtt.service` (systemd **--user**). A cópia instalada fica em `~/.config/systemd/user/claude-usage-mqtt.service`, com caminho absoluto `/home/frussa/Clawdmeter-CYD/...`. Mudou a do repo? Copie, rode `systemctl --user daemon-reload` e `systemctl --user restart claude-usage-mqtt`. Isso reinicia um serviço em uso: só com o OK do Carlos.
- Log: `journalctl --user -u claude-usage-mqtt -f`.

## Build e gravação

```bash
./build_cyd.sh                # pio run -e esp32_cyd; copia clawdmeter_cyd.bin para a raiz
./build_cyd.sh /dev/ttyUSB0   # compila e grava (921600 baud)
```

- Env único: `esp32_cyd` (`board = esp32dev`, partição `huge_app.csv`, pioarduino `55.03.38-1`, que traz o Arduino Core 3.x exigido pela GFX Library).
- O PlatformIO **não está instalado** no n8n-srv (2026-11-06). O `build_cyd.sh` instala via `pip`, se faltar.
- A porta `/dev/ttyUSB0` é o padrão do script. Confirme com `ls /dev/ttyUSB* /dev/ttyACM*` com a placa ligada.
- Monitor serial: 115200. O firmware imprime `{"ready":true}` no boot e a config MQTT (senha mascarada).

## Gotchas

1. **DRAM:** BLE (NimBLE) e Wi-Fi não cabem juntos no ESP32 clássico. Os buffers LVGL são alocados no heap (10 linhas × 2, modo PARTIAL). Não aumente sem medir.
2. **Fontes LVGL 9:** o `lv_font_conv` gera formato LVGL 8. Para usar, tire os guards `#if LVGL_VERSION_MAJOR >= 8` e o `.cache`, e acrescente `.release_glyph`, `.kerning`, `.static_bitmap`, `.fallback` e `.user_data`. Sem isso a fonte fica invisível.
3. **Touch centralizado:** `touch_read()` roda uma vez por loop; o LVGL lê o estado compartilhado. Não chame `touch.getPoint()` em outro lugar.
4. **Ícones RGB565A8 são planares** (`w*h` RGB565 + `w*h` alpha). Conversor: `tools/png_to_lvgl.js` (tinge de branco por padrão).
5. **Splash:** `node tools/scrape_claudepix.js` e depois `node tools/convert_to_c.js` geram `splash_animations.h`. As animações são do claudepix.vercel.app (assets de terceiros: não desenhar arte própria no lugar).

## Legado do upstream (não vale para o CYD)

- `flash.sh` (SC01 Plus), `flash-mac.sh`, `install.sh` e `install-mac.sh` (instalam o daemon **BLE**).
- `daemon/claude-usage-daemon.{sh,service}`, `claude_usage_daemon.py` e `com.user.claude-usage-daemon.plist`: transporte BLE.
- `screenshot.sh`: depende de um comando serial `screenshot` que **não existe** neste firmware. Para validar a UI no CYD, olhe a placa ou o log serial.
- `README.md`: descreve o upstream.

## Git

- Branch `main`, que acompanha `origin/main` do fork. Commit e push só com o OK do Carlos.
- Antes de commitar, rode `git diff` e confira se não entrou segredo.
