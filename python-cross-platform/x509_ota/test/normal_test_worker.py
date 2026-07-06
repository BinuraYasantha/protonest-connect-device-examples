def run(app):
    print("X.509 normal OTA test worker version {} started".format(app.version))
    app.mark_healthy()

    sequence = 0
    while not app.is_stopping():
        print(
            "X.509 normal OTA test worker running: version={} sequence={}".format(
                app.version,
                sequence,
            )
        )
        sequence += 1
        app.sleep(app.publish_interval_seconds)
