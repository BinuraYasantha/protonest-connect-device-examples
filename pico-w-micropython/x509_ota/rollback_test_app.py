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

    print("Rollback test app started")
    print("This app will crash before calling on_healthy()")
    print("OTA rollback should trigger after repeated failed boots")

    for count in range(5):
        if led is not None:
            led.value(0 if led.value() else 1)
        print("Rollback test countdown:", 5 - count)
        time.sleep(1)

    raise RuntimeError("Intentional rollback test crash")
