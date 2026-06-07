target_sources_ifdef(CONFIG_PAW3395          app PRIVATE lariska_rate.c)
target_sources_ifdef(CONFIG_LARISKA_BLE_LED  app PRIVATE lariska_ble_led.c)
