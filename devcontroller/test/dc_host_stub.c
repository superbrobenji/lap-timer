/* Keeps the dchost static lib non-empty before any component adds pure host sources
 * (see the host directory convention in this dir's CMakeLists). Each parallel
 * component adds its real pure logic under its own component host tree. */
int dc_host_stub__(void) { return 0; }
