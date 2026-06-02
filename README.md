# AllStarLink RPI Hardware Interface

A Raspberry Pi daemon that bridges [AllStarLink](https://www.allstarlink.org/) node activity to physical hardware. It tails Asterisk's node-activity log in real time, tracks RX/TX key state for each connected node in a red-black tree, and drives GPIO LEDs and buttons through a JSON-configured Hardware Abstraction Layer (HAL).

Runs as a systemd service that automatically restarts on failure.

---

## How It Works

```
Asterisk → node_activity log → Listener thread → LogAction queue
                                                        ↓
                                              Main loop drains queue
                                                        ↓
                                            Red-black tree (node state)
                                                        ↓
                                              HAL → GPIO LEDs / Buttons
```

1. **Listener** — A background thread tails the daily Asterisk activity log (`/var/log/asterisk/node_activity/<node>/<YYYYMMDD>.txt`), filters lines matching `RXKEY`, `TXKEY`, `RXUNKEY`, `TXUNKEY`, `LINKTRX`, `LINKMONITOR`, and `LINKDISC`, and queues them as `LogAction` items.
2. **Main loop** — Drains the queue, looks up or creates each node in a red-black tree, and updates its state.
3. **HAL** — A thin abstraction over wiringPi that exposes buttons and LEDs by logical name. The JSON config maps names to GPIO pins and driver types at runtime.

---

## Prerequisites

### System dependencies

```bash
sudo apt install libjson-c-dev wiringpi
```

These are installed automatically by `make` if missing.

### Enable Asterisk activity logging

1. Open your rpt.conf:
   ```bash
   sudo nano /etc/asterisk/rpt.conf
   ```

2. Find the stanza for your node (e.g. `[443240]`) and add:
   ```ini
   archivedir = /var/log/asterisk/node_activity  ; log destination
   archiveaudio = 0                              ; avoids filling the SD card
   ```

3. Restart Asterisk:
   ```bash
   sudo systemctl restart asterisk
   ```

4. Grant read access to the log directory:
   ```bash
   sudo chmod 755 /var/log/asterisk/node_activity/
   ```

5. Verify log output (replace `443240` with your node number):
   ```bash
   tail -f /var/log/asterisk/node_activity/443240/$(date +%Y%m%d).txt \
     | grep -E "RXKEY|TXKEY|RXUNKEY|TXUNKEY"
   ```

### Enable I2C (if using an MCP23017 expander)

```bash
sudo raspi-config
# Interface Options → I2C → Enable
```

---

## Configuration

Config files are installed to `/etc/asl-interface/` by `make`.

### `HardwareDefinitions.json`

Declares every physical button and LED. Pin numbers use the **wiringPi** numbering scheme.

```json
[
    {
        "type": "gpioButton",
        "pin": 7,
        "logicalName": "button1",
        "debounceTimeMS": 40,
        "pull": "off",
        "interrupt": "both"
    },
    {
        "type": "gpioLed",
        "pin": 1,
        "logicalName": "led1"
    }
]
```

| Field | Description |
|---|---|
| `type` | Driver type: `gpioButton` or `gpioLed` |
| `pin` | wiringPi pin number |
| `logicalName` | Name used to reference this device in `AppConfig.json` |
| `debounceTimeMS` | (buttons only) Debounce window in milliseconds |
| `pull` | (buttons only) `"up"`, `"down"`, or `"off"` |
| `interrupt` | (buttons only) `"rising"`, `"falling"`, or `"both"` |

### `AppConfig.json`

Maps AllStarLink nodes to LED/button channels. LED values can be a `logicalName` from `HardwareDefinitions.json` or `"none"`.

```json
{
    "appConfigVersion": "1.0",
    "mainTxLed": "led1",
    "channelMappings": [
        {
            "channelNumber": 1,
            "node": "2324",
            "conLed": "led2",
            "txLed": "none",
            "rxLed": "led3",
            "button": "button1"
        }
    ]
}
```

| Field | Description |
|---|---|
| `mainTxLed` | LED that reflects the local node's TX state |
| `mainRxLed` | LED that reflects the local node's RX state |
| `channelMappings` | One entry per monitored remote node |
| `node` | AllStarLink node number |
| `conLed` | Lights when the node is connected |
| `txLed` | Lights when the remote node is transmitting |
| `rxLed` | Lights when the remote node is receiving |
| `button` | Button that controls this channel |

### `Nodes.json`

Optional friendly-name table for node numbers.

```json
[
    { "node": "2324", "friendlyname": "K8SN" }
]
```

---

## Build & Install

All targets require `sudo` (wiringPi and `/usr/bin` write access).

| Target | Description |
|---|---|
| `make all` | Compile, install configs, install and start the systemd service |
| `make debug` | Compile and install configs; does **not** start the service |
| `make debugValgrind` | Debug build, then run under Valgrind with leak checking |
| `make clean` | Remove compiled object files |

### Full install

```bash
sudo make all
```

Installs the binary to `/usr/bin/asl-interface`, installs configs to `/etc/asl-interface/`, and enables the systemd service.

### Debug (no service)

```bash
sudo make debug
sudo /usr/bin/asl-interface
```

### Memory leak check

```bash
sudo make debugValgrind
```

Runs Valgrind with `--leak-check=full` and `--track-origins=yes`. Known false positives from glibc's POSIX timer helper thread and wiringPi's ISR threads are suppressed via `asl-interface.supp`.

---

## Service Management

The daemon runs as `asl-interface.service` and restarts automatically on failure.

```bash
sudo systemctl status asl-interface    # check status
sudo systemctl stop asl-interface      # stop
sudo systemctl start asl-interface     # start
sudo journalctl -u asl-interface -f    # follow logs
```

---

## Acknowledgments

- **Xie Qing** — red-black tree implementation: [xieqing/red-black-tree](https://github.com/xieqing/red-black-tree)
