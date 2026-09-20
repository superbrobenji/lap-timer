/* Keeps the dchost static lib non-empty before any components/<c>/host source files exist
 * (harness setup). Each parallel component adds its real pure logic under components/<c>/host/. */
int dc_host_stub__(void) { return 0; }
