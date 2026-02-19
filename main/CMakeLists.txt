idf_component_register(
    SRCS "main.c" "ui.c" "wifi.c" "mqtt.c" "img_bg.c"
          "fonts/font_sv_16.c"
          "fonts/font_sv_18.c"
          "fonts/font_sv_28.c"
          "fonts/font_sv_36.c"
    INCLUDE_DIRS "." "fonts"
)
