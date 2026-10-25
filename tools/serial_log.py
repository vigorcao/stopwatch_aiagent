"""Reset the StopWatch over USB-Serial-JTAG and print its log for N seconds.

pyserial asserts DTR/RTS on open by default, which holds the ESP32-S3 in reset
over USB-Serial-JTAG, so both lines are released before opening the port.
"""
import os, sys, time, serial

args = [a for a in sys.argv[1:] if not a.startswith("--")]
port = args[0] if args else "/dev/cu.usbmodem1101"
secs = float(args[1]) if len(args) > 1 else 10

def open_port():
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.2
    s.dtr = False
    s.rts = False
    s.open()
    return s

if "--reset" in sys.argv:
    # hard-reset can leave the chip in download mode; a watchdog reset boots the app.
    import subprocess, glob
    esptool = glob.glob(os.path.expanduser("~/Library/Arduino15/packages/m5stack/tools/esptool_py/*/esptool"))[0]
    subprocess.run([esptool, "--chip", "esp32s3", "-p", port, "--after", "watchdog-reset", "chip-id"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
t0, s = time.time(), None
while time.time() - t0 < secs:
    try:
        if s is None:
            s = open_port()
        data = s.read(4096)
        if data:
            sys.stdout.write(data.decode(errors="replace")); sys.stdout.flush()
    except (serial.SerialException, OSError):
        s = None
        time.sleep(0.05)
