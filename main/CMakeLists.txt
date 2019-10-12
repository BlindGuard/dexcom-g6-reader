set(COMPONENT_SRCS "main.c"
                   "util.c"
                   "messages.c"
                   "gatt.c"
                   "characteristics.c"
                   "dexcom_g6_reader.h")
set(COMPONENT_ADD_INCLUDEDIRS ".")

register_component()
