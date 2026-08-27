import serial
import pynmea2
import math
import time

#SERIAL_PORT = "COM9"      # Windows 
SERIAL_PORT = "/dev/ttyACM0" # linux
# SERIAL_PORT = "/dev/ttyUSB0"   # Linux example
BAUD_RATE = 115200

def haversine(lat1, lon1, lat2, lon2):
    R = 6372800.0  # Earth radius in meters
    phi1 = math.radians(lat1)
    phi2 = math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlambda = math.radians(lon2 - lon1)

    a = math.sin(dphi / 2.0) ** 2 + \
        math.cos(phi1) * math.cos(phi2) * math.sin(dlambda / 2.0) ** 2
    c = 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))
    return R * c

ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)

prev_lat = None
prev_lon = None
total_distance = 0.0

print("Reading GNSS data... Press Ctrl+C to stop.")

try:
    while True:
        line = ser.readline().decode("ascii", errors="replace").strip()

        if not line.startswith("$"):
            continue

        try:
            msg = pynmea2.parse(line)
        except pynmea2.ParseError:
            continue

        lat = None
        lon = None

        if isinstance(msg, pynmea2.types.talker.RMC):
            if msg.status == "A":
                lat = msg.latitude
                lon = msg.longitude

        elif isinstance(msg, pynmea2.types.talker.GGA):
            if int(msg.gps_qual or 0) > 0:
                lat = msg.latitude
                lon = msg.longitude

        if lat is not None and lon is not None:
            if prev_lat is not None and prev_lon is not None:
                step_distance = haversine(prev_lat, prev_lon, lat, lon)
                print(f"step distance: {step_distance:.7f}m")
                if 0.2 < step_distance < 5.0:
                    total_distance += step_distance
                
                print(f"Lat: {lat:.7f}, Lon: {lon:.7f}, Total Distance: {total_distance:.2f} m")

            prev_lat = lat
            prev_lon = lon

        time.sleep(0.01)

except KeyboardInterrupt:
    print(f"\nFinal distance = {total_distance:.2f} meters")
    ser.close()