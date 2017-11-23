if enable_mosquitto
    libmqttagent = declare_dependency (
        include_directories : include_directories ('.'),
        link_with : library (
            'mqttagent',
            'mqttagent.c',
            'mqttclient.c',
            dependencies : [libmosquitto, liblaik],
        ),
    )
endif
