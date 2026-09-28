if(CONFIG_HA_AUTO_HOST)
  target_sources(app PRIVATE ${ZEPHYR_CURRENT_MODULE_DIR}/src/ha_auto_host.c)
endif()
