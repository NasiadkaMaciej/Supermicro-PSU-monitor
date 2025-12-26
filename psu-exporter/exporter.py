import serial
import json
import time
import os
import threading
from prometheus_client import start_http_server, Gauge

# Environment configuration
SERIAL_PORT = os.getenv('SERIAL_PORT', '/dev/ttyUSB0')
BAUD_RATE = int(os.getenv('BAUD_RATE', 115200))
EXPORTER_PORT = int(os.getenv('EXPORTER_PORT', 8000))
STALENESS_TIMEOUT = int(os.getenv('STALENESS_TIMEOUT', 10))  # seconds

# Prometheus metrics definitions
TEMP = Gauge('psu_temperature_celsius', 'PSU Temperature', ['psu_id'])
FAN_SPEED = Gauge('psu_fan_speed_rpm', 'PSU Fan Speed', ['psu_id', 'fan_index'])
VOLTAGE = Gauge('psu_input_voltage_volts', 'Input Voltage', ['psu_id'])
CURRENT = Gauge('psu_input_current_amps', 'Input Current', ['psu_id'])
POWER = Gauge('psu_input_power_watts', 'Input Power', ['psu_id'])
STATUS = Gauge('psu_status_ok', 'PSU Status (1=OK, 0=Fail)', ['psu_id'])

# Track last update time for each PSU
psu_last_seen = {}
psu_lock = threading.Lock()

def clear_psu_metrics(psu_id):
    """Clear metrics for a given PSU"""
    try:
        TEMP._metrics.pop((psu_id,), None)
        FAN_SPEED._metrics.pop((psu_id, '1'), None)
        FAN_SPEED._metrics.pop((psu_id, '2'), None)
        VOLTAGE._metrics.pop((psu_id,), None)
        CURRENT._metrics.pop((psu_id,), None)
        POWER._metrics.pop((psu_id,), None)
        STATUS._metrics.pop((psu_id,), None)
        print(f"Cleared metrics for PSU #{psu_id}", flush=True)
    except Exception as e:
        print(f"Error clearing metrics for PSU #{psu_id}: {e}", flush=True)

def check_stale_psus():
    """Check and remove metrics for PSUs that stopped sending data"""
    while True:
        time.sleep(5)
        current_time = time.time()
        with psu_lock:
            stale_psus = []
            for psu_id, last_seen in list(psu_last_seen.items()):
                if current_time - last_seen > STALENESS_TIMEOUT:
                    stale_psus.append(psu_id)
            
            for psu_id in stale_psus:
                clear_psu_metrics(psu_id)
                del psu_last_seen[psu_id]

def process_line(line):
    try:
        data = json.loads(line)
        psu_id = str(data['id'])

        with psu_lock:
            psu_last_seen[psu_id] = time.time()
        TEMP.labels(psu_id=psu_id).set(data['temp'])
        FAN_SPEED.labels(psu_id=psu_id, fan_index='1').set(data['fan1'])
        FAN_SPEED.labels(psu_id=psu_id, fan_index='2').set(data['fan2'])
        VOLTAGE.labels(psu_id=psu_id).set(data['v_in'])
        CURRENT.labels(psu_id=psu_id).set(data['i_in'])
        POWER.labels(psu_id=psu_id).set(data['p_in'])
        STATUS.labels(psu_id=psu_id).set(data['ok'])
        
        print(f"Updated metrics for PSU #{psu_id}", flush=True)
    except json.JSONDecodeError:
        print(f"Invalid JSON: {line}", flush=True)
    except Exception as e:
        print(f"Error: {e}", flush=True)

def main():
    print(f"Starting PSU Exporter on port {EXPORTER_PORT} reading from {SERIAL_PORT}")
    print(f"PSU staleness timeout: {STALENESS_TIMEOUT}s")
    start_http_server(EXPORTER_PORT)
    
    staleness_thread = threading.Thread(target=check_stale_psus, daemon=True)
    staleness_thread.start()
    while True:
        try:
            with serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1) as ser:
                print(f"Connected to {SERIAL_PORT}")
                while True:
                    line = ser.readline().decode('utf-8').strip()
                    if line:
                        process_line(line)
        except Exception as e:
            print(f"Connection lost: {e}. Retrying in 5s...")
            time.sleep(5)

if __name__ == '__main__':
    main()
