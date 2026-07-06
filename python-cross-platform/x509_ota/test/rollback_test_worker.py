def run(app):
    print("X.509 rollback test worker version {} started".format(app.version))
    raise RuntimeError("Intentional rollback test failure before healthy signal")
