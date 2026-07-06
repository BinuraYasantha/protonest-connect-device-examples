import time

try:
    from machine import Pin
except ImportError:
    Pin = None


def create_led():
    if Pin is None:
        return None

    try:
        return Pin("LED", Pin.OUT)
    except Exception:
        try:
            return Pin(25, Pin.OUT)
        except Exception:
            return None


def run(on_healthy=None):
    led = create_led()

    print("Application started")
    print("Application health check in progress...")
    time.sleep(2)

    if on_healthy is not None:
        on_healthy()

    print("Application marked healthy")

    while True:
        if led is not None:
            led.value(0 if led.value() else 1)
        print("Application heartbeat")
        time.sleep(0.1)
