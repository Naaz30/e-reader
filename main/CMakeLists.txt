idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES it8951-esp32 page esp_driver_gpio esp_hw_support
                       EMBED_TXTFILES "sample.txt" "lighthouse.txt")
