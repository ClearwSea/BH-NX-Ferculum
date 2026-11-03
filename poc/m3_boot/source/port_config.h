/* port_config.h -- M2 loader identity. MIT. */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

#if M3_STANDALONE
#define PORT_TITLE   "Houkai 3rd 2017"
#define PORT_NAME    "bh3_2017"
#define M3_PYTHON_ROOT "/switch/bh3_2017/python"
#define M3_SERVER_ROOT "/switch/bh3_2017/server-stage3"
#else
#define PORT_TITLE   "BH3 M2 Loader"
#define PORT_NAME    "bh3_test"
#define M3_PYTHON_ROOT "/switch/bh3_python_probe"
#define M3_SERVER_ROOT "/switch/bh3_test/server-stage3"
#endif
#define PORT_PACKAGE "com.miHoYo.enterprise.NGHSoI"
#define DCR_PKG_NAME PORT_PACKAGE

/* libunity + libmain + libil2cpp; the loader maps them into this region. */
#define PORT_SO_REGION_BYTES (160u * 1024 * 1024)

/* libAkSoundEngine uses OpenSL ES; the runtime implements it on audout. */
#define RT_OPENSLES 1

#endif /* PORT_CONFIG_H */
