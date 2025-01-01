instalar:
libgtk-3-dev
libappindicator3-dev
libjson-c-dev

para compilar:
gcc dockermonitor.c -o dockermonitor `pkg-config --cflags --libs gtk+-3.0 appindicator3-0.1 json-c`
