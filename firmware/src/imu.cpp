#include "imu.h"

// No IMU on ESP32 CYD — stubs that satisfy the interface.
void    imu_init(void)         {}
void    imu_tick(void)         {}
uint8_t imu_get_rotation(void) { return 0; }
