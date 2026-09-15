#include "core/exp.h"
int exp_nmea_open(exp_t *e) { (void)e; return -1; }
int exp_nmea_feed(exp_t *e, uint8_t type, const uint8_t *p, uint8_t len) { (void)e; (void)type; (void)p; (void)len; return -1; }
int exp_nmea_finish(exp_t *e) { (void)e; return -1; }
