IF (NOT X11_FOUND)
    PLUGIN_DISABLE ()
ELSE ()
    PLUGIN_INCLUDE_DIRECTORIES (${cross}/plugins/FilePlugin ${cross}/plugins/SocketPlugin)
ENDIF ()
