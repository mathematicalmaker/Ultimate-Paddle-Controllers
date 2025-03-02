#include <Arduino.h>

#include <ezButton.h> 
#include <HID-Project.h>
#include <FastLED.h>

// Debug level 2 = all messages
// Debug level 1 = some messages
// Debug level 0 = no messages
#define DEBUG 0

#if DEBUG==2
#define outputDebug2(x); Serial.print(x);
#define outputDebugLine2(x); Serial.println(x);
#define outputDebug(x); Serial.print(x);
#define outputDebugLine(x); Serial.println(x);
#elif DEBUG==1
#define outputDebug(x); Serial.print(x);
#define outputDebugLine(x); Serial.println(x);
#define outputDebug2(x); 
#define outputDebugLine2(x); 
#else
#define outputDebug(x); 
#define outputDebugLine(x); 
#define outputDebug2(x); 
#define outputDebugLine2(x); 
#endif

#define EncX_PHASE_A_PIN 10  //Green
#define EncX_PHASE_B_PIN 9   //White
#define EncX_BUTTON1_PIN 8   //Fire button
#define EncX_BUTTON2_PIN 7   //Settings button

#define EncY_PHASE_A_PIN 3  //Green
#define EncY_PHASE_B_PIN 4  //White
#define EncY_BUTTON1_PIN 5  //Fire button
#define EncY_BUTTON2_PIN 6  //Settings button

#define MODE_SELECT_PIN 2    //Switch for Mouse<->Joystick mode
#define GAMEPAD_CENTER_PIN 0 //Button to center joystick for apps that need it

#define LED_PIN 1         //WS2812B LEDs are connected here
#define LED_NUMPIXELS 4 
#define LED_BRIGHTNESS 16  //0-255 (Not perceived linearly)
#define LED_X_DIR   0      //LEDs are numbered 0-3, each paddle has a direction LED
#define LED_X_SPEED 1      //and a speed LED
#define LED_Y_DIR   2
#define LED_Y_SPEED 3

#define SPEED_1_COLOR CRGB::DarkRed
#define SPEED_2_COLOR CRGB::Yellow
#define SPEED_3_COLOR CRGB::DarkGreen
#define NORMAL_DIRECTION_COLOR CRGB::DarkGreen
#define REVERSE_DIRECTION_COLOR CRGB::DarkRed

#define DIRECTION_CW  0   // clockwise direction (used for debugging)
#define DIRECTION_CCW 1  // counter-clockwise direction (used for debugging)

enum Mode { GAMEPAD, MOUSE };
Mode mode = MOUSE; // Initialize with MOUSE mode

// Define the array of leds
CRGB leds[LED_NUMPIXELS];

const int debounce_time = 10;
const unsigned long shortPress = 60; // Short press of settings button to change direction
const unsigned long longPress = 500; // Long press of settings button to change speed

// These values can be changed to affect "base" sensitivity
// Multipliers for changing direction and speed
const int mouseStepBase = 1;    // This gets multiplied by the step multiplier
const int gamepadStepBase = 40; // This gets multiplied by the step multiplier

// Limits are defined by HID-Project
const int gamepadMinval=-32768;
const int gamepadMaxval=32767;

ezButton buttonCenter(GAMEPAD_CENTER_PIN);

// Axis data structure
struct AxisData {
  // Values changed by ISR need to be volatile
  volatile int counter;    
  volatile int direction;
  int prevCounter;    // Tracking for encoder changes
  int stepMultiplier; // Cycles 1-3: 1=slow, 2=medium, 3=fast
  int dirMultiplier;  // Direction is +/-1 for normal/reverse
  int dirLedIndex;
  int speedLedIndex;
  ezButton button1;
  ezButton button2;
  // Tracking variables for long/short presses of settings buttons
  unsigned long button2PressedTime;
  unsigned long button2PressedDuration;
  bool button2Pressed;
  bool longPressTriggered;
  // Gamepad position for this axis
  int gamepadPos; 
};

// Axis data
AxisData axisX = {
  0,            // Initialize counter
  DIRECTION_CW, // Initialize direction
  0,            // Initialize prevCounter
  2,            // Initialize stepMultiplier
  1,            // Initialize dirMultiplier
  LED_X_DIR,    // Initialize dirLedIndex
  LED_X_SPEED,  // Initialize speedLedIndex
  ezButton(EncX_BUTTON1_PIN),
  ezButton(EncX_BUTTON2_PIN),
  0,            // Initialize button2PressedTime
  0,            // Initialize button2PressedDuration
  false,        // Initialize longPressProcessed
  false,        // Initialize button2Pressed
  0             // Initialize gamepadPos
};

AxisData axisY = {
  0,            // Initialize counter
  DIRECTION_CW, // Initialize direction
  0,            // Initialize prevCounter
  2,            // Initialize stepMultiplier
  1,            // Initialize dirMultiplier
  LED_Y_DIR,    // Initialize dirLedIndex
  LED_Y_SPEED,  // Initialize speedLedIndex
  ezButton(EncY_BUTTON1_PIN),
  ezButton(EncY_BUTTON2_PIN),
  0,            // Initialize button2PressedTime
  0,            // Initialize button2PressedDuration
  false,         // Initialize longPressProcessed
  0             // Initialize gamepadPos
};

// Function declarations
void handleButtonPress(ezButton& button, int buttonId, int mouseButton, int gamepadButton);
void handleSettingsButton(AxisData& axis);
void handleEncoderChange(AxisData& axis, int& gamepadPos, bool isXAxis);

// Interrupt Service Routine declarations
void ISR_encoderXChange();
void ISR_encoderYChange();

void setup() {
  Serial.begin(115200);
  pinMode(MODE_SELECT_PIN, INPUT_PULLUP);

  // Set the mode based on the mode select pin
  if (digitalRead(MODE_SELECT_PIN) == LOW ) {
    outputDebugLine("Gamepad Mode");
    mode = GAMEPAD;
    Gamepad.begin();
  } else {
    outputDebugLine("Mouse Mode");
    mode = MOUSE;
    Mouse.begin();
  }

  // configure encoder pins as inputs -- pullup is required!
  pinMode(EncX_PHASE_A_PIN, INPUT_PULLUP);
  pinMode(EncX_PHASE_B_PIN, INPUT_PULLUP);
  pinMode(EncY_PHASE_A_PIN, INPUT_PULLUP);
  pinMode(EncY_PHASE_B_PIN, INPUT_PULLUP);

  // Set debounce for buttons
  buttonCenter.setDebounceTime(debounce_time);
  axisX.button1.setDebounceTime(debounce_time);
  axisX.button2.setDebounceTime(debounce_time);
  axisY.button1.setDebounceTime(debounce_time);
  axisY.button2.setDebounceTime(debounce_time);

  // Initialize status LEDs
  FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, LED_NUMPIXELS);  // GRB ordering is assumed
  FastLED.setBrightness(LED_BRIGHTNESS);
  leds[LED_X_DIR] = NORMAL_DIRECTION_COLOR;
  leds[LED_Y_DIR] = NORMAL_DIRECTION_COLOR;
  leds[LED_X_SPEED] = SPEED_2_COLOR;
  leds[LED_Y_SPEED] = SPEED_2_COLOR;
  FastLED.show();

  // A single interrupt for Phase A pin is enough
  // call ISR_encoderChange() when Phase A pin changes from LOW to HIGH
  attachInterrupt(digitalPinToInterrupt(EncX_PHASE_A_PIN), ISR_encoderXChange, RISING);
  attachInterrupt(digitalPinToInterrupt(EncY_PHASE_A_PIN), ISR_encoderYChange, RISING);
  
  // Reboot on mode change using NVIC_SystemReset
  // This works with the Seeeduino XIAO SAMD21, others may need a different function
  attachInterrupt(digitalPinToInterrupt(MODE_SELECT_PIN), NVIC_SystemReset, CHANGE);
}

void loop() {
  // Update buttons
  axisX.button1.loop(); axisX.button2.loop();
  axisY.button1.loop(); axisY.button2.loop();
  buttonCenter.loop();

  // Handle button presses
  handleButtonPress(axisX.button1, 1, MOUSE_LEFT, 1);
  handleButtonPress(axisY.button1, 2, MOUSE_RIGHT, 2);

  // Handle settings buttons
  handleSettingsButton(axisX);
  handleSettingsButton(axisY);

  // Handle center button
  if (buttonCenter.isPressed() && mode == GAMEPAD) {
    outputDebugLine("Gamepad center button is pressed");
    axisX.gamepadPos = 0;
    axisY.gamepadPos = 0;
    outputDebugLine("Gamepad centered");
  }

  // Handle encoder changes
  handleEncoderChange(axisX, axisX.gamepadPos, true);  // true for X-axis
  handleEncoderChange(axisY, axisY.gamepadPos, false); // false for Y-axis

  // Gamepad write
  if (mode == GAMEPAD) {
    Gamepad.xAxis(axisX.gamepadPos);
    Gamepad.yAxis(axisY.gamepadPos);
    Gamepad.write();
  }
}

void handleButtonPress(ezButton& button, int buttonId, int mouseButton, int gamepadButton) {
  if (button.isPressed()) {
    outputDebug("Button "); outputDebug(buttonId); outputDebugLine(" is pressed");
    if (mode == MOUSE) {
      Mouse.press(mouseButton);
    } else {
      Gamepad.press(gamepadButton);
    }
  }
  if (button.isReleased()) {
    outputDebug("Button "); outputDebug(buttonId); outputDebugLine(" is released");
    if (mode == MOUSE) {
      Mouse.release(mouseButton);
    } else {
      Gamepad.release(gamepadButton);
    }
  }
}

void handleSettingsButton(AxisData& axis) {
  if (axis.button2.isPressed()) {
    if (!axis.button2Pressed) { // First press
      axis.button2PressedTime = millis();
      axis.button2Pressed = true;
      axis.longPressTriggered = false; // Reset long press flag
      outputDebug("Button "); outputDebug(axis.button2Pin); outputDebugLine(" is pressed");
    }
  }

  if (axis.button2Pressed && !axis.longPressTriggered) { 
    // Calculate duration only if still pressed and long press not triggered
    axis.button2PressedDuration = millis() - axis.button2PressedTime;

    if (axis.button2PressedDuration > longPress) {
      // Long press - change speed immediately
      axis.stepMultiplier = (axis.stepMultiplier % 3) + 1;
      switch (axis.stepMultiplier) {
        case 1: leds[axis.speedLedIndex] = SPEED_1_COLOR; break;
        case 2: leds[axis.speedLedIndex] = SPEED_2_COLOR; break;
        case 3: leds[axis.speedLedIndex] = SPEED_3_COLOR; break;
      }
      FastLED.show();
      outputDebug("Button "); outputDebug(axis.button2Pin); outputDebugLine(" longpress detected");
      outputDebug("New combined multiplier: ");
      outputDebugLine(axis.dirMultiplier * axis.stepMultiplier);
      axis.longPressTriggered = true; // Set long press flag
    }
  }

  if (axis.button2.isReleased()) {
    if (axis.button2PressedDuration >= shortPress && axis.button2PressedDuration <= longPress) {
      // Short press - reverse direction
      axis.dirMultiplier *= -1;
      leds[axis.dirLedIndex] = (axis.dirMultiplier < 0) ? REVERSE_DIRECTION_COLOR : NORMAL_DIRECTION_COLOR;
      FastLED.show();
      outputDebug("Button "); outputDebug(axis.button2Pin); outputDebugLine(" shortpress detected");
      outputDebug("New combined multiplier: ");
      outputDebugLine(axis.dirMultiplier * axis.stepMultiplier);
    }
    axis.button2Pressed = false; // Reset button pressed flag
    axis.button2PressedDuration = 0; // Reset duration on release.
    outputDebug("Button "); outputDebug(axis.button2Pin); outputDebugLine(" is released");
  }
}

void handleEncoderChange(AxisData& axis, int& gamepadPos, bool isXAxis) {
  if (axis.prevCounter != axis.counter) {
    outputDebug2("DIRECTION: ");
    outputDebug2((axis.direction == DIRECTION_CW) ? "Clockwise" : "Counter-clockwise");
    outputDebug2(" | COUNTER: ");
    outputDebugLine2(axis.counter);

    int delta = axis.counter - axis.prevCounter;
    int moveAmount = axis.dirMultiplier * axis.stepMultiplier * delta;

    if (mode == MOUSE) {
      if (isXAxis) {
        Mouse.move(moveAmount * mouseStepBase, 0, 0);
      } else {
        Mouse.move(0, moveAmount * mouseStepBase, 0);
      }
    } else {
      gamepadPos += moveAmount * gamepadStepBase;
      gamepadPos = constrain(gamepadPos, gamepadMinval, gamepadMaxval);
    }
    axis.prevCounter = axis.counter;
  }
}

void ISR_encoderXChange() {
  // Phase A has gone from low to high, so we only need to check Phase B
  if (digitalRead(EncX_PHASE_B_PIN) == HIGH) {
    axisX.counter--; axisX.direction = DIRECTION_CCW;
  } else {
    axisX.counter++; axisX.direction = DIRECTION_CW;
  }
}

void ISR_encoderYChange() {
  // Phase A has gone from low to high, so we only need to check Phase B
  if (digitalRead(EncY_PHASE_B_PIN) == HIGH) {
    axisY.counter--; axisY.direction = DIRECTION_CCW;
  } else {
    axisY.counter++; axisY.direction = DIRECTION_CW;
  }
}

