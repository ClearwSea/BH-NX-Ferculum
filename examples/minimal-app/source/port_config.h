/* port_config.h -- the M1 probe's identity. MIT.
 *
 * The runtime's required settings only. This probe never opens an APK; the
 * package name is here so the same header can grow into the real port.
 */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

#define PORT_TITLE   "BH3 M1 Probe"
#define PORT_NAME    "bh3_test"
#define PORT_PACKAGE "com.miHoYo.enterprise.NGHSoI"

#define PORT_SO_REGION_BYTES (48u * 1024 * 1024)

#endif /* PORT_CONFIG_H */
