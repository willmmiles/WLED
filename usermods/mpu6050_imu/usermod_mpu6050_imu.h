#pragma once

#include "wled.h"

/* This driver reads quaternion data from the MPU6060 and adds it to the JSON
   This example is adapted from:
   https://github.com/jrowberg/i2cdevlib/tree/master/Arduino/MPU6050/examples/MPU6050_DMP6_ESPWiFi

   Tested with a d1 mini esp-12f

  GY-521  NodeMCU
  MPU6050 devkit 1.0
  board   Lolin         Description
  ======= ==========    ====================================================
  VCC     VU (5V USB)   Not available on all boards so use 3.3V if needed.
  GND     G             Ground
  SCL     D1 (GPIO05)   I2C clock
  SDA     D2 (GPIO04)   I2C data
  XDA     not connected
  XCL     not connected
  AD0     not connected
  INT     D8 (GPIO15)   Interrupt pin
  
  Using usermod:
  1. Copy the usermod into the sketch folder (same folder as wled00.ino)
  2. Register the usermod by adding #include "usermod_filename.h" in the top and registerUsermod(new MyUsermodClass()) in the bottom of usermods_list.cpp
  3. I2Cdev and MPU6050 must be installed as libraries, or else the .cpp/.h file 
     for both classes must be in the include path of your project. To install the
     libraries add I2Cdevlib-MPU6050@fbde122cc5 to lib_deps in the platformio.ini file.
  4. You also need to change lib_compat_mode from strict to soft in platformio.ini (This ignores that I2Cdevlib-MPU6050 doesn't list platform compatibility)
  5. Wire up the MPU6050 as detailed above.
*/

#include "I2Cdev.h"

#include "MPU6050_6Axis_MotionApps20.h"
//#include "MPU6050.h" // not necessary if using MotionApps include file

// Arduino Wire library is required if I2Cdev I2CDEV_ARDUINO_WIRE implementation
// is used in I2Cdev.h
#if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
    #include "Wire.h"
#endif

// ================================================================
// ===               INTERRUPT DETECTION ROUTINE                ===
// ================================================================

volatile bool mpuInterrupt = false;     // indicates whether MPU interrupt pin has gone high
void IRAM_ATTR dmpDataReady() {
    mpuInterrupt = true;
}


class MPU6050Driver : public Usermod {
  private:
    static const char _name[];

    MPU6050 mpu;
    bool enabled = true;

    // MPU control/status vars
    bool dmpReady = false;  // set true if DMP init was successful
    uint8_t mpuIntStatus;   // holds actual interrupt status byte from MPU
    uint8_t devStatus;      // return status after each device operation (0 = success, !0 = error)
    uint16_t packetSize;    // expected DMP packet size (default is 42 bytes)
    uint16_t fifoCount;     // count of all bytes currently in FIFO
    uint8_t fifoBuffer[64]; // FIFO storage buffer

    // calibration values
    int16_t gyro_offset[3]; // 94, -20, -20 for katana;  53,  -18, 30 for test board
    int16_t accel_offset[3];  // test board: -1250, -6433, 1345

    //NOTE: some of these can be removed to save memory, processing time
    //      if the measurement isn't needed
    Quaternion qat;         // [w, x, y, z]         quaternion container
    float euler[3];         // [psi, theta, phi]    Euler angle container
    float ypr[3];           // [yaw, pitch, roll]   yaw/pitch/roll container
    VectorInt16 aa;         // [x, y, z]            accel sensor measurements
    VectorInt16 gy;         // [x, y, z]            gyro sensor measurements
    VectorInt16 aaReal;     // [x, y, z]            gravity-free accel sensor measurements
    VectorInt16 aaWorld;    // [x, y, z]            world-frame accel sensor measurements
    VectorFloat gravity;    // [x, y, z]            gravity vector

    static const int INTERRUPT_PIN = 15; // use pin 15 on ESP8266

  public:
    //Functions called by WLED

    /*
     * setup() is called once at boot. WiFi is not yet connected at this point.
     */
    void setup() {
      if (i2c_scl<0 || i2c_sda<0) { enabled = false; DEBUG_PRINTLN(F("MPU6050: I2C is no good."));  return; }
      #if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
        Wire.setClock(400000U); // 400kHz I2C clock. Comment this line if having compilation difficulties
      #elif I2CDEV_IMPLEMENTATION == I2CDEV_BUILTIN_FASTWIRE
        Fastwire::setup(400, true);
      #endif

      // initialize device
      DEBUG_PRINTLN(F("Initializing I2C devices..."));
      mpu.initialize();
      //pinMode(INTERRUPT_PIN, INPUT);

      // verify connection
      DEBUG_PRINTLN(F("Testing device connections..."));
      DEBUG_PRINTLN(mpu.testConnection() ? F("MPU6050 connection successful") : F("MPU6050 connection failed"));

      // load and configure the DMP
      DEBUG_PRINTLN(F("Initializing DMP..."));
      devStatus = mpu.dmpInitialize();

      // supply your own gyro offsets here, scaled for min sensitivity
      mpu.setXGyroOffset(gyro_offset[0]);
      mpu.setYGyroOffset(gyro_offset[1]);
      mpu.setZGyroOffset(gyro_offset[2]);
      mpu.setXAccelOffset(accel_offset[0]);
      mpu.setYAccelOffset(accel_offset[1]);
      mpu.setZAccelOffset(accel_offset[2]);
      
      // make sure it worked (returns 0 if so)
      if (devStatus == 0) {
        // turn on the DMP, now that it's ready
        DEBUG_PRINTLN(F("Enabling DMP..."));
        mpu.setDMPEnabled(true);

        // set our DMP Ready flag so the main loop() function knows it's okay to use it
        DEBUG_PRINTLN(F("DMP ready!"));
        dmpReady = true;

        // get expected DMP packet size for later comparison
        packetSize = mpu.dmpGetFIFOPacketSize();
      } else {
        // ERROR!
        // 1 = initial memory load failed
        // 2 = DMP configuration updates failed
        // (if it's going to break, usually the code will be 1)
        DEBUG_PRINT(F("DMP Initialization failed (code "));
        DEBUG_PRINT(devStatus);
        DEBUG_PRINTLN(")");
      }
    }

    /*
     * connected() is called every time the WiFi is (re)connected
     * Use it to initialize network interfaces
     */
    void connected() {
      //DEBUG_PRINTLN("Connected to WiFi!");
    }


    /*
     * loop() is called continuously. Here you can check for events, read sensors, etc.
     */
    void loop() {
      // if programming failed, don't try to do anything
      if (!enabled || !dmpReady || strip.isUpdating()) return;

      // reset interrupt flag and get INT_STATUS byte
      mpuIntStatus = mpu.getIntStatus();

      // get current FIFO count
      fifoCount = mpu.getFIFOCount();

      // check for overflow (this should never happen unless our code is too inefficient)
      if ((mpuIntStatus & 0x10) || fifoCount == 1024) {
        // reset so we can continue cleanly
        mpu.resetFIFO();
        DEBUG_PRINTLN(F("FIFO overflow!"));

        // otherwise, check for DMP data ready interrupt (this should happen frequently)
      } else if (mpuIntStatus & 0x02) {
        // wait for correct available data length, should be a VERY short wait
        if (fifoCount < packetSize) return; // next time

        // read a packet from FIFO
        mpu.getFIFOBytes(fifoBuffer, packetSize);

        //NOTE: some of these can be removed to save memory, processing time
        //      if the measurement isn't needed
        mpu.dmpGetQuaternion(&qat, fifoBuffer);
        mpu.dmpGetEuler(euler, &qat);
        mpu.dmpGetGravity(&gravity, &qat);
        mpu.dmpGetGyro(&gy, fifoBuffer);
        mpu.dmpGetAccel(&aa, fifoBuffer);
        mpu.dmpGetLinearAccel(&aaReal, &aa, &gravity);
        mpu.dmpGetLinearAccelInWorld(&aaWorld, &aaReal, &qat);
        mpu.dmpGetYawPitchRoll(ypr, &qat, &gravity);
      }
    }



    void addToJsonInfo(JsonObject& root)
    {
      JsonObject user = root["u"];
      if (user.isNull()) user = root.createNestedObject("u");

      JsonObject imu_meas = user.createNestedObject("IMU");
      JsonArray quat_json = imu_meas.createNestedArray("Quat");
      quat_json.add(qat.w);
      quat_json.add(qat.x);
      quat_json.add(qat.y);
      quat_json.add(qat.z);
      JsonArray euler_json = imu_meas.createNestedArray("Euler");
      euler_json.add(euler[0]);
      euler_json.add(euler[1]);
      euler_json.add(euler[2]);
      JsonArray accel_json = imu_meas.createNestedArray("Accel");
      accel_json.add(aa.x);
      accel_json.add(aa.y);
      accel_json.add(aa.z);
      JsonArray gyro_json = imu_meas.createNestedArray("Gyro");
      gyro_json.add(gy.x);
      gyro_json.add(gy.y);
      gyro_json.add(gy.z);
      JsonArray world_json = imu_meas.createNestedArray("WorldAccel");
      world_json.add(aaWorld.x);
      world_json.add(aaWorld.y);
      world_json.add(aaWorld.z);
      JsonArray real_json = imu_meas.createNestedArray("RealAccel");
      real_json.add(aaReal.x);
      real_json.add(aaReal.y);
      real_json.add(aaReal.z);
      JsonArray grav_json = imu_meas.createNestedArray("Gravity");
      grav_json.add(gravity.x);
      grav_json.add(gravity.y);
      grav_json.add(gravity.z);
      JsonArray orient_json = imu_meas.createNestedArray("Orientation");
      orient_json.add(ypr[0]);
      orient_json.add(ypr[1]);
      orient_json.add(ypr[2]);
    }


    /*
     * addToJsonState() can be used to add custom entries to the /json/state part of the JSON API (state object).
     * Values in the state object may be modified by connected clients
     */
    //void addToJsonState(JsonObject& root)
    //{
      //root["user0"] = userVar0;
    //}


    /*
     * readFromJsonState() can be used to receive data clients send to the /json/state part of the JSON API (state object).
     * Values in the state object may be modified by connected clients
     */
    //void readFromJsonState(JsonObject& root)
    //{
      //if (root["bri"] == 255) DEBUG_PRINTLN(F("Don't burn down your garage!"));
    //}


    /*
     * addToConfig() can be used to add custom persistent settings to the cfg.json file in the "um" (usermod) object.
     * It will be called by WLED when settings are actually saved (for example, LED settings are saved)
     * I highly recommend checking out the basics of ArduinoJson serialization and deserialization in order to use custom settings!
     */
    void addToConfig(JsonObject& root)
    {
      JsonObject top = root.createNestedObject(FPSTR(_name));

      //save these vars persistently whenever settings are saved
      top["x_acc_bias"] = accel_offset[0];
      top["y_acc_bias"] = accel_offset[1];
      top["z_acc_bias"] = accel_offset[2];
      top["x_gyro_bias"] = gyro_offset[0];
      top["y_gyro_bias"] = gyro_offset[1];
      top["z_gyro_bias"] = gyro_offset[2];
    }

    /*
     * readFromConfig() can be used to read back the custom settings you added with addToConfig().
     * This is called by WLED when settings are loaded (currently this only happens immediately after boot, or after saving on the Usermod Settings page)
     * 
     * readFromConfig() is called BEFORE setup(). This means you can use your persistent values in setup() (e.g. pin assignments, buffer sizes),
     * but also that if you want to write persistent values to a dynamic buffer, you'd need to allocate it here instead of in setup.
     * If you don't know what that is, don't fret. It most likely doesn't affect your use case :)
     * 
     * Return true in case the config values returned from Usermod Settings were complete, or false if you'd like WLED to save your defaults to disk (so any missing values are editable in Usermod Settings)
     * 
     * getJsonValue() returns false if the value is missing, or copies the value into the variable provided and returns true if the value is present
     * The configComplete variable is true only if the "exampleUsermod" object and all values are present.  If any values are missing, WLED will know to call addToConfig() to save them
     * 
     * This function is guaranteed to be called on boot, but could also be called every time settings are updated
     */
    bool readFromConfig(JsonObject& root)
    {
      // default settings values could be set here (or below using the 3-argument getJsonValue()) instead of in the class definition or constructor
      // setting them inside readFromConfig() is slightly more robust, handling the rare but plausible use case of single value being missing after boot (e.g. if the cfg.json was manually edited and a value was removed)

      JsonObject top = root[FPSTR(_name)];

      bool configComplete = !top.isNull();

      configComplete &= getJsonValue(top["x_acc_bias"], accel_offset[0], 0);
      configComplete &= getJsonValue(top["y_acc_bias"], accel_offset[1], 0);
      configComplete &= getJsonValue(top["z_acc_bias"], accel_offset[2], 0);
      configComplete &= getJsonValue(top["x_gyro_bias"], gyro_offset[0], 0);
      configComplete &= getJsonValue(top["y_gyro_bias"], gyro_offset[1], 0);
      configComplete &= getJsonValue(top["z_gyro_bias"], gyro_offset[2], 0);

      if (dmpReady) {
        mpu.setXGyroOffset(gyro_offset[0]);
        mpu.setYGyroOffset(gyro_offset[1]);
        mpu.setZGyroOffset(gyro_offset[2]);
        mpu.setXAccelOffset(accel_offset[0]);
        mpu.setYAccelOffset(accel_offset[1]);
        mpu.setZAccelOffset(accel_offset[2]);
      }

      return configComplete;
    }


    /*
     * appendConfigData() is called when user enters usermod settings page
     * it may add additional metadata for certain entry fields (adding drop down is possible)
     * be careful not to add too much as oappend() buffer is limited to 3k
     */
    void appendConfigData()
    {

    }


    /*
     * getId() allows you to optionally give your V2 usermod an unique ID (please define it in const.h!).
     */
    uint16_t getId()
    {
      return USERMOD_ID_IMU;
    }

};


const char MPU6050Driver::_name[] PROGMEM = "MPU6050_IMU";