def run(app):
    print("PSK default OTA worker version {} started".format(app.version))
    app.mark_healthy()

    sequence = 0
    while not app.is_stopping():
        print(
            "PSK default OTA worker running: version={} sequence={}".format(
                app.version,
                sequence,
            )
        )
        sequence += 1
        app.sleep(app.publish_interval_seconds)
