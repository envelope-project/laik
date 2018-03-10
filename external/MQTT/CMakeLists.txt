if (mosquitto-agent)
    find_library (mosquitto "mosquitto")

    if (mosquitto)
        add_library (mqttagent SHARED
            "mqttagent.c"
            "mqttclient.c"
        )

        target_include_directories (mqttagent
            PUBLIC "."
        )

        target_link_libraries (mqttagent
            PRIVATE "laik"
            PRIVATE "${mosquitto}"
        )
    elseif (skip-missing)
        message (STATUS "Dependency check for option 'mosquitto-agent' failed, skipping!")
    else ()
        message (FATAL_ERROR "Dependency check for option 'mosquitto-agent' failed, stopping!")
    endif ()
endif ()
