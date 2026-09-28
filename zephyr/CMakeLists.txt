if(CONFIG_HA_AUTO_HOST)
  zephyr_library()
  zephyr_library_sources(${ZEPHYR_CURRENT_MODULE_DIR}/src/ha_auto_host.c)
endif()
