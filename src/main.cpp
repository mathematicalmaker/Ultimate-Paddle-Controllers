#include <Arduino.h>

#include <ezButton.h> 
#include <HID-Project.h>
#include <FastLED.h>

// Debug level 2 = all messages
// Debug level 1 = some messages
// Debug level 2 = no messages
#define DEBUG 1

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
#define EncX_BUTTON1_PIN 8
#define EncX_BUTTON2_PIN 7

#define EncY_PHASE_A_PIN 3  //Green
#define EncY_PHASE_B_PIN 4   //White
#define EncY_BUTTON1_PIN 5
#define EncY_BUTTON2_PIN 6

#define MODE_SELECT_PIN 2
#define GAMEPAD_CENTER_PIN 0

#define LED_PIN 1
#define LED_NUMPIXELS 4 
#define LED_BRIGHTNESS 32 //0-255
#define LED_X_DIR 0
#define LED_X_SPEED 3
#define LED_Y_DIR 1
#define LED_Y_SPEED 2

#define SPEED_1_COLOR CRGB::DarkRed
#define SPEED_2_COLOR CRGB::Yellow
#define SPEED_3_COLOR CRGB::DarkGreen
#define NORMAL_DIRECTION_COLOR CRGB::DarkGreen
#define REVERSE_DIRECTION_COLOR CRGB::DarkRed

#define shortPress 250 
#define longPress 1000

#define DIRECTION_CW 0   // clockwise direction
#define DIRECTION_CCW 1  // counter-clockwise direction

#define GAMEPAD_MODE 0
#define MOUSE_MODE 1

// Define the array of leds
CRGB leds[LED_NUMPIXELS];

ezButton buttonX1(EncX_BUTTON1_PIN);
ezButton buttonY1(EncY_BUTTON1_PIN);
ezButton buttonX2(EncX_BUTTON2_PIN);
ezButton buttonY2(EncY_BUTTON2_PIN);
ezButton buttonCenter(GAMEPAD_CENTER_PIN);

int mode = MOUSE_MODE;

int stepMultiplierX = 2;
int stepMultiplierY = 2;
int dirMultiplierX = 1;
int dirMultiplierY = 1;
const int mouseStepBase = 1;
const int gamepadStepBase = 50;
const int debounce_time = 10;

volatile int counterX = 0;
volatile int directionX = DIRECTION_CW;
volatile int counterY = 0;
volatile int directionY = DIRECTION_CW;

int prev_counterX;
int prev_counterY;
int gamepadXpos = 0;
int gamepadYpos = 0;
const int gamepadMinval=-32768;
const int gamepadMaxval=32767;

unsigned long buttonX2PressedTime = 0;
unsigned long buttonY2PressedTime = 0;
long buttonX2PressedDuration;
long buttonY2PressedDuration;

void ISR_encoderXChange();
void ISR_encoderYChange();

void setup() {
  Serial.begin(115200);
  pinMode(MODE_SELECT_PIN, INPUT_PULLUP);
  if (digitalRead(MODE_SELECT_PIN) == LOW ) {
    outputDebugLine("Gamepad Mode");
    mode=GAMEPAD_MODE;
    Gamepad.begin();
  } else {
    outputDebugLine("Mouse Mode");
    mode=MOUSE_MODE;
    Mouse.begin();
  }

  // configure encoder pins as inputs
  pinMode(EncX_PHASE_A_PIN, INPUT_PULLUP);
  pinMode(EncX_PHASE_B_PIN, INPUT_PULLUP);
  pinMode(EncY_PHASE_A_PIN, INPUT_PULLUP);
  pinMode(EncY_PHASE_B_PIN, INPUT_PULLUP);

  // Set debounce for buttons
  buttonX1.setDebounceTime(debounce_time);  
  buttonY1.setDebounceTime(debounce_time);  
  buttonX2.setDebounceTime(debounce_time);  
  buttonY2.setDebounceTime(debounce_time);
  buttonCenter.setDebounceTime(debounce_time);

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
  // Reboot on mode change
  attachInterrupt(digitalPinToInterrupt(MODE_SELECT_PIN), NVIC_SystemReset, CHANGE);
}

void loop() {
  buttonX1.loop();  
  buttonY1.loop();  
  buttonX2.loop();
  buttonY2.loop();
  buttonCenter.loop();

  if(buttonX1.isPressed()) {   
    outputDebugLine("Button X1 is pressed");
    if(mode==MOUSE_MODE) {
      Mouse.press(MOUSE_LEFT);
    } else {
      Gamepad.press(1);  
    }
  }

  if(buttonX1.isReleased()) {
    outputDebugLine("Button X1 is released");
    if(mode==MOUSE_MODE) {
      Mouse.release(MOUSE_LEFT);
    } else {
      Gamepad.release(1);  
    }
  }

  if(buttonY1.isPressed()) {
    outputDebugLine("Button Y1 is pressed");
    if(mode==MOUSE_MODE) {
      Mouse.press(MOUSE_RIGHT);
    } else {
      Gamepad.press(2);  
    }
  }

  if(buttonY1.isReleased()) {
    outputDebugLine("Button Y1 is released");
    if(mode==MOUSE_MODE) {
      Mouse.release(MOUSE_RIGHT);
    } else {
      Gamepad.release(2);  
    }
  }

  if(buttonX2.isPressed()) {
    buttonX2PressedTime = millis();
    outputDebugLine("Button X2 is pressed");
  }

  if(buttonX2.isReleased()) {
    buttonX2PressedDuration = millis() - buttonX2PressedTime;
    outputDebugLine("Button X2 is released");
    if(buttonX2PressedDuration > shortPress) {
      if(buttonX2PressedDuration > longPress) {
        //Long press - change speed
        stepMultiplierX = (stepMultiplierX%3) + 1;
        switch(stepMultiplierX) {
          case 1:
            leds[LED_X_SPEED] = SPEED_1_COLOR;
            break;
          case 2:
            leds[LED_X_SPEED] = SPEED_2_COLOR;
            break;
          case 3:
            leds[LED_X_SPEED] = SPEED_3_COLOR;
            break;
        }
        outputDebugLine("Button X2 longpress detected");
      } else {
        //Short press - reverse direction
        dirMultiplierX *= -1;
        if(dirMultiplierX < 0) {
          leds[LED_X_DIR] = REVERSE_DIRECTION_COLOR;
        } else {
          leds[LED_X_DIR] = NORMAL_DIRECTION_COLOR;
        }
        outputDebugLine("Button X2 shortpress detected");
      }
      FastLED.show();
      outputDebug("New X combined multiplier: ");
      outputDebugLine(dirMultiplierX * stepMultiplierX);
    } else {
      outputDebugLine("Button press X2 Ignored (< ShortPress)");
    }
  }  

  if(buttonY2.isPressed()) {
    buttonY2PressedTime = millis();
    outputDebugLine("Button Y2 is pressed");
  }

  if(buttonY2.isReleased()) {
    buttonY2PressedDuration = millis() - buttonY2PressedTime;
    outputDebugLine("Button Y2 is released");
    if(buttonY2PressedDuration > shortPress) {
      if(buttonY2PressedDuration > longPress) {
        //Long press - change speed
        stepMultiplierY = (stepMultiplierY%3) + 1;
        switch(stepMultiplierY) {
          case 1:
            leds[LED_Y_SPEED] = SPEED_1_COLOR;
            break;
          case 2:
            leds[LED_Y_SPEED] = SPEED_2_COLOR;
            break;
          case 3:
            leds[LED_Y_SPEED] = SPEED_3_COLOR;
            break;
        }
        outputDebugLine("Button Y2 longpress detected");
      } else {
        //Short press - reverse direction
        dirMultiplierY *= -1;
        if(dirMultiplierY < 0) {
          leds[LED_Y_DIR] = REVERSE_DIRECTION_COLOR;
        } else {
          leds[LED_Y_DIR] = NORMAL_DIRECTION_COLOR;
        }
        outputDebugLine("Button Y2 shortpress detected");
      }
      FastLED.show();
      outputDebug("New Y combined multiplier: ");
      outputDebugLine(dirMultiplierY * stepMultiplierY);
    } else {
      outputDebugLine("Button press Y2 Ignored (<ShortPress)");
    }
  }

  if(buttonCenter.isPressed()) {
    outputDebugLine("Gamepad center button is pressed");
    if(mode==GAMEPAD_MODE) {
      gamepadXpos = 0;
      gamepadYpos = 0;
      Gamepad.xAxis(gamepadXpos);
      Gamepad.yAxis(gamepadYpos);
      outputDebugLine("Gamepad centered");
    } 
  }

  if (prev_counterX != counterX) {
    outputDebug2("DIRECTION X: ");
    if (directionX == DIRECTION_CW) {
      outputDebug2("Clockwise");
    } else {
      outputDebug2("Counter-clockwise");
    }
    outputDebug2(" | COUNTER X: ");
    outputDebugLine2(counterX);
    if(mode == MOUSE_MODE) {
      Mouse.move(dirMultiplierX*stepMultiplierX*mouseStepBase*(counterX-prev_counterX),0,0);
    } else {
      //counter1 = prev_counter1+stepMultiplier*gamepadStepBase*(counter1-prev_counter1);
      gamepadXpos += dirMultiplierX*stepMultiplierX*gamepadStepBase*(counterX-prev_counterX);
      if (gamepadXpos<gamepadMinval) {
        gamepadXpos=gamepadMinval;
      } else if (gamepadXpos > gamepadMaxval) {
        gamepadXpos = gamepadMaxval;
      }
      Gamepad.xAxis(gamepadXpos);
    }
    prev_counterX = counterX;
  }

  if (prev_counterY != counterY) {
    outputDebug2("DIRECTION Y: ");
    if (directionY == DIRECTION_CW) {
      outputDebug2("Clockwise");
    } else {
      outputDebug2("Counter-clockwise");
    }
    outputDebug2(" | COUNTER Y: ");
    outputDebugLine2(counterY);

    if(mode == MOUSE_MODE) {
      Mouse.move(0,dirMultiplierY*stepMultiplierY*mouseStepBase*(counterY-prev_counterY),0);
    } else {
      gamepadYpos += dirMultiplierY*stepMultiplierY*gamepadStepBase*(counterY-prev_counterY);
      if (gamepadYpos<gamepadMinval) {
        gamepadYpos=gamepadMinval;
      } else if (gamepadYpos > gamepadMaxval) {
        gamepadYpos = gamepadMaxval;
      }
      Gamepad.yAxis(gamepadYpos);
    }
    prev_counterY = counterY;
  }

  if(mode == GAMEPAD_MODE){
    Gamepad.write();
  }
}

void ISR_encoderXChange() {
  if (digitalRead(EncX_PHASE_B_PIN) == HIGH) {
    // the encoder is rotating in counter-clockwise direction => decrease the counter
    counterX--;
    directionX = DIRECTION_CCW;
  } else {
    // the encoder is rotating in clockwise direction => increase the counter
    counterX++;
    directionX = DIRECTION_CW;
  }
}

void ISR_encoderYChange() {
  if (digitalRead(EncY_PHASE_B_PIN) == HIGH) {
    // the encoder is rotating in counter-clockwise direction => decrease the counter
    counterY--;
    directionY = DIRECTION_CCW;
  } else {
    // the encoder is rotating in clockwise direction => increase the counter
    counterY++;
    directionY = DIRECTION_CW;
  }
}

