#ifndef IMU_SENSOR_H
#define IMU_SENSOR_H

void mpuInit();
void mpuUpdate(float &accelX, float &accelY, float &accelZ);

#endif