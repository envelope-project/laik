if (enable-mosquitto-agent)
    add_library (mqttagent SHARED
        "mqttagent.c"
        "mqttclient.c"
    )

    target_include_directories (mqttagent
        PUBLIC "."
    )

    target_link_libraries (mqttagent
        PRIVATE "laik"
        PRIVATE "mosquitto"
    )
endif (enable-mosquitto-agent)
