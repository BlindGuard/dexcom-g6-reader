set(COMPONENT_SRCS "main.c"
                   "util.c"
                   "messages.c"
                   "gatt.c"
                   "gatt_lists.c"
                   "storage.c"
                   "dexcom_g6_reader.h")
set(COMPONENT_ADD_INCLUDEDIRS ".")

register_component()
