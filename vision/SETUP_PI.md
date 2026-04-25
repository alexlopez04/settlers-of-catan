# Raspberry Pi Setup — Catan CV

End-to-end guide for deploying the Catan computer-vision pipeline on a
Raspberry Pi with a live camera, GPIO UART output to the Arduino Mega, and
an optional debug display.

---

## Hardware requirements

| Component | Notes |
|---|---|
| Raspberry Pi 4B (recommended) | Pi 3B+ or Zero 2W also work; Pi 5 uses `/dev/ttyAMA0` too |
| Pi Camera Module v2 / v3 | USB webcam also works — pass `--camera 0` |
| HDMI display | Optional; only needed for the debug UI |
| 3.3 V ↔ 5 V bidirectional level-shifter | Required for UART — Pi GPIO is 3.3 V, Mega is 5 V. BSS138-based boards (e.g. Adafruit 757) work well |
| Dupont jumper wires | 4 wires: TX, RX, GND, 3.3 V for shifter |

### Wiring

```
Pi GPIO 14 (TXD) ──► [LV side] 3.3↔5V shifter [HV side] ──► Arduino Mega pin 17 (RX2)
Pi GPIO 15 (RXD) ◄── [LV side] 3.3↔5V shifter [HV side] ◄── Arduino Mega pin 16 (TX2)
Pi pin 17 (3.3 V) ─── LV power on shifter
Pi pin 6  (GND)   ─── GND on shifter (and share GND with Mega)
Mega 5 V          ─── HV power on shifter
```

---

## 1 · Flash the OS

Use **Raspberry Pi Imager** (https://www.raspberrypi.com/software/).

- OS: **Raspberry Pi OS (64-bit)** — use the *Lite* variant for headless
  deployment; use *Desktop* if you want the debug UI on a local screen.
- In the imager's "Advanced options" (⚙️ gear icon), set:
  - Hostname, SSH key/password
  - Wi-Fi credentials (if needed)

Flash to SD card, insert, and boot.

---

## 2 · System update

```bash
sudo apt update && sudo apt full-upgrade -y
sudo reboot
```

---

## 3 · Enable the camera

```bash
sudo raspi-config
```

Navigate to: **Interface Options → Camera → Enable**

(On Pi 5 / Bookworm with `libcamera` this step may not be needed — the camera
is enabled by default via the device-tree overlay.)

---

## 4 · Enable GPIO hardware UART

The GPIO UART (pins 14/15, `/dev/ttyAMA0`) is the PL011 full UART and is
much more reliable than the mini-UART at 115 200 baud.

### Pi 3 / 4 / 5 — free the PL011 from Bluetooth

Edit the boot config:

```bash
# Pi 4 / 5
sudo nano /boot/firmware/config.txt

# Pi 3 (older Buster/Bullseye images)
sudo nano /boot/config.txt
```

Add at the bottom of the file:

```ini
enable_uart=1
dtoverlay=disable-bt
```

`disable-bt` reassigns the PL011 UART back to GPIO 14/15 and disables
Bluetooth. If you need Bluetooth, use `dtoverlay=miniuart-bt` instead (swaps
the UARTs without disabling BT, but the mini-UART is less stable).

### Remove the serial console

The kernel defaults to using the UART as a login console. Remove it so
`pyserial` can open the port cleanly:

```bash
# Pi 4 / 5
sudo nano /boot/firmware/cmdline.txt

# Pi 3
sudo nano /boot/cmdline.txt
```

Find and **remove** (only these tokens — keep everything else on one line):

```
console=serial0,115200
```

### Reboot and verify

```bash
sudo reboot
ls -l /dev/ttyAMA0      # should exist and be owned by dialout
```

---

## 5 · Add your user to the `dialout` group

```bash
sudo usermod -a -G dialout $USER
# Log out and back in (or reboot) for the change to take effect.
```

---

## 6 · Install Python dependencies

On Pi, install OpenCV and NumPy from the system repository (pre-built with
hardware acceleration) rather than pip:

```bash
sudo apt install -y \
    python3-opencv \
    python3-numpy \
    python3-picamera2 \
    python3-pip

# pyserial is not in the default repo at a recent enough version — use pip:
pip3 install --break-system-packages pyserial>=3.5
```

> **Note:** `python3-picamera2` is only available on Raspberry Pi OS
> Bookworm and later. On Bullseye use `sudo apt install -y python3-picamera2`
> after adding the Pi repository, or fall back to `opencv-python` from pip
> (the code detects which backend is available automatically).

---

## 7 · Transfer / clone the project

```bash
# Option A — git clone
git clone <your-repo-url> /home/pi/settlers-of-catan

# Option B — rsync from dev machine
rsync -avz --exclude='__pycache__' --exclude='*.pyc' \
    /path/to/settlers-of-catan/vision/ \
    pi@<pi-ip>:/home/pi/settlers-of-catan/vision/
```

---

## 8 · Smoke-test from the command line

```bash
cd /home/pi/settlers-of-catan

# Test with a static image (no camera or serial needed)
python3 -m vision.pi_serial --simulate vision/tests/board_generator.py

# Test the debug UI with a static image (requires DISPLAY or HDMI)
python3 -m vision.pi_debug --simulate <path-to-board-photo.jpg> --no-serial

# Live camera, display-only (check alignment before wiring serial)
python3 -m vision.pi_debug --no-serial

# Full pipeline — camera + UART to Arduino
python3 -m vision.pi_debug --port /dev/ttyAMA0

# Headless full pipeline (no display)
python3 -m vision.pi_serial --port /dev/ttyAMA0 --verbose
```

---

## 9 · Autorun with systemd

Two service files are provided below — one headless, one with the debug UI.
Install whichever fits your deployment.

### 9a · Headless service (`catan-cv.service`)

```bash
sudo nano /etc/systemd/system/catan-cv.service
```

```ini
[Unit]
Description=Catan CV — headless board-state detector
After=network.target
# Ensure the camera device is ready before starting.
After=dev-video0.device

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi/settlers-of-catan
ExecStart=/usr/bin/python3 -m vision.pi_serial \
    --port /dev/ttyAMA0 \
    --baud 115200 \
    --interval 0.5 \
    --verbose
Restart=always
RestartSec=5
# Prevent rapid restart loops from hammering the system.
StartLimitIntervalSec=60
StartLimitBurst=5
StandardOutput=journal
StandardError=journal
Environment=PYTHONUNBUFFERED=1
Environment=PYTHONPATH=/home/pi/settlers-of-catan

[Install]
WantedBy=multi-user.target
```

Enable and start:

```bash
sudo systemctl daemon-reload
sudo systemctl enable catan-cv.service
sudo systemctl start  catan-cv.service

# Check status
sudo systemctl status catan-cv.service

# Follow logs in real time
journalctl -u catan-cv.service -f
```

---

### 9b · Debug UI service (`catan-cv-debug.service`)

This requires a desktop session running (Pi OS Desktop or a bare Xorg
session started at boot).

**Option 1 — systemd user service (recommended for Desktop installs)**

```bash
mkdir -p ~/.config/systemd/user
nano ~/.config/systemd/user/catan-cv-debug.service
```

```ini
[Unit]
Description=Catan CV Debug UI
After=graphical-session.target

[Service]
Type=simple
WorkingDirectory=/home/pi/settlers-of-catan
ExecStart=/usr/bin/python3 -m vision.pi_debug \
    --port /dev/ttyAMA0 \
    --baud 115200 \
    --interval 0.5 \
    --fullscreen
Restart=on-failure
RestartSec=5
StandardOutput=journal
StandardError=journal
Environment=PYTHONUNBUFFERED=1
Environment=PYTHONPATH=/home/pi/settlers-of-catan
Environment=DISPLAY=:0

[Install]
WantedBy=default.target
```

```bash
systemctl --user daemon-reload
systemctl --user enable catan-cv-debug.service
systemctl --user start  catan-cv-debug.service

# Enable lingering so user services start even without interactive login:
sudo loginctl enable-linger pi

# Logs
journalctl --user -u catan-cv-debug.service -f
```

**Option 2 — LXDE/XFCE autostart (Desktop, even simpler)**

```bash
mkdir -p ~/.config/autostart
nano ~/.config/autostart/catan-cv-debug.desktop
```

```ini
[Desktop Entry]
Type=Application
Name=Catan CV Debug
Exec=/usr/bin/python3 -m vision.pi_debug --port /dev/ttyAMA0 --fullscreen
WorkingDirectory=/home/pi/settlers-of-catan
Environment=PYTHONPATH=/home/pi/settlers-of-catan
X-GNOME-Autostart-enabled=true
```

This launches automatically when the Pi desktop session starts.

---

## 10 · Keyboard controls (debug UI)

| Key | Action |
|---|---|
| `q` / `Esc` | Quit |
| `m` | Toggle HSV marker-mask overlay on the raw camera panel |
| `p` | Pause / resume detection (keeps last frame on screen) |

---

## 11 · Troubleshooting

### Serial port permission denied
```bash
sudo usermod -a -G dialout pi
# Reboot or log out/in.
ls -l /dev/ttyAMA0   # should show group dialout
```

### `/dev/ttyAMA0` not found
Verify the overlay applied:
```bash
dtoverlay -l      # should list disable-bt
```
If not, double-check the config.txt edits and reboot.

### Camera not detected
```bash
# Pi Camera Module
vcgencmd get_camera         # should show: supported=1 detected=1
libcamera-hello --list-cameras

# USB camera
ls /dev/video*
v4l2-ctl --list-devices
```

### Debug UI shows a blank/black window on HDMI
The `DISPLAY` variable may not be set when running as a service.
```bash
export DISPLAY=:0
xhost +local:root          # allow root (or the service user) to draw
python3 -m vision.pi_debug --no-serial
```

### "Alignment failed" / "Expected 4 reference markers, found N"
- Ensure all four magenta corner stickers are within the camera's field of view.
- Check the raw-camera panel in the debug UI — detected markers appear as
  magenta crosshairs. Press `m` to see the HSV mask and tune colors if needed.
- Adjust lighting: the detector uses HSV thresholds tuned for bright magenta
  under white light. Avoid strong coloured ambient lighting.

### High CPU usage on Pi Zero / old Pi
Increase the detection interval to reduce frame rate:
```bash
python3 -m vision.pi_debug --interval 1.0
```

### View live logs for headless service
```bash
journalctl -u catan-cv.service -n 100 --no-pager
journalctl -u catan-cv.service -f       # stream
```

---

## Quick-reference: common commands

```bash
# Start / stop / restart headless service
sudo systemctl start   catan-cv.service
sudo systemctl stop    catan-cv.service
sudo systemctl restart catan-cv.service

# Disable autorun
sudo systemctl disable catan-cv.service

# Check last 50 log lines
journalctl -u catan-cv.service -n 50 --no-pager

# Test serial connection manually (Arduino must be powered)
python3 -c "
import serial, time
s = serial.Serial('/dev/ttyAMA0', 115200, timeout=1)
print('Port opened:', s.name)
time.sleep(0.5)
s.close()
"

# Force re-detection interval to 1 fps for diagnostics
python3 -m vision.pi_serial --port /dev/ttyAMA0 --interval 1.0 --verbose
```
