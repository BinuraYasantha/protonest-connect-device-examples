def run(app):
    print("PSK rollback test worker version {} started".format(app.version))
    raise RuntimeError("Intentional rollback test failure before healthy signal")
