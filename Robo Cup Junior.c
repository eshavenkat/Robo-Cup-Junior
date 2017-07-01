#pragma config(Sensor, S1,     LLEADER,        sensorEV3_GenericI2C)
#pragma config(Sensor, S2,     ColorL,         sensorEV3_Color, modeEV3Color_Color)
#pragma config(Sensor, S3,     ColorR,         sensorEV3_Color, modeEV3Color_Color)
#pragma config(Sensor, S4,     BUMPER,         sensorEV3_Touch)
#pragma config(Motor,  motorB,           ,     tmotorEV3_Large, openLoop, reversed, encoder)
#pragma config(Motor,  motorC,           ,     tmotorEV3_Large, openLoop, reversed, encoder)
//*!!Code automatically generated by 'ROBOTC' configuration wizard               !!*//

#include "mindsensors-lineleader.h"
/***********************************************************************************************
Team: RoboWarriors
 ____            _                __        __                        _
|  _ \    ___   | |__     ___     \ \      / /   __ _   _ __   _ __  (_)   ___    _ __   ___
| |_) |  / _ \  | '
|  _ <  | (_) | | |_) | | (_) |     \ V  V /   | (_| | | |    | |    | | | (_) | | |    \__ \
|_| \_\  \___/  |_.__/   \___/       \_/\_/     \__,_| |_|    |_|    |_|  \___/  |_|    |___/

************************************************************************************************/

bool debug = true;

short sensor = 255;
tByteArray llVal;
int sens[8]; // holds black or white pixel values for line leader
float weighted_error = 0;  // weighted average of errors seen so far
float error_lambda = .99; // a factor that determines how much we remember past errors - high number means we remember an old error longer
float normalized_error = 0; // scaled error

int baseSpeed = 20; 			// speed of robot on straight line
int gain = 9; 						// how fast does it turns

int error_accum=0;

int whitespace = 0;
int black_begin = -1; // the first pixel on line leader that is black
int black_end = -1; // the last pixel on line leader that is black

int turn = 0; // direction and magnitude of turn
int turn_time = 0; // how long we turn for

//----------------------------------------
// color sensor related
//----------------------------------------
int currentColorL;
int currentColorR;
const int GREEN_COLOR = 3;
const int BLACK_COLOR = 1;
int seen_green_right = 0;
int seen_green_left = 0;
const int FORGET_COLOR_SENSOR = 150; // how long to ignore color sensor for a short period after the turn

float left_motor_speed = 0;
float right_motor_speed = 0;

const int INJECTION = 3; // error value we add to regular error to cause turn
const int INJECTION_TIME = 100; // how long we hold error value
//int read_color_sensor = 0; // whether or not we are reading color sensor now - (deactivated just after turn because we can get noisy values)
int ignore_Color_Sensor_While_Turning = 0; // whether or not we are reading color sensor now - (deactivated just after turn because we can get noisy values)
const int IGNORE_COLOR_SENSOR_FOR_SOMETIME = 51; // how long to ignore color sensor for a short period after the turn

//----------------------------------------
// Robot turn error handling
//----------------------------------------
const int arrayErrorsLength = 8;
int arrayErrors[arrayErrorsLength];
void ShiftArrayError();
int SumArrayError();
void ResetArrayError();
void PrintArrayError();

void turnBack(); // turn back on seeing two greens left and right of the line

bool bLeftOrRight = true; // go right or left around obstacle ==> true = right, false = left
void avoidObstacle(bool bLeftOrRight);

bool keep_line_following = false; // follow line?


task main()
{

	//----------------------------------------
	ResetArrayError();
	if (debug) PrintArrayError();
	//----------------------------------------

	while (true) {

		setLEDColor(ledOrangeFlash);

		if (bLeftOrRight) displayCenteredBigTextLine(1, "RIGHT");
		else displayCenteredBigTextLine(1, "LEFT");

		displayBigTextLine(5, "<<<Left");
		displayBigTextLine(8, "       Right>>>");
		displayCenteredBigTextLine(13, "Mid=START");

		if (getButtonPress(buttonLeft)) bLeftOrRight = false;
		if (getButtonPress(buttonRight)) bLeftOrRight = true;
		if (getButtonPress(buttonEnter)) {
			keep_line_following = true;
			eraseDisplay();
		}

		while (keep_line_following) { // start rescue line

			setLEDColor(ledGreenFlash);
			displayCenteredBigTextLine(8, "RescueLine");
			if (getButtonPress(buttonBack)) keep_line_following = false;

			// avoid obstacle
			if (SensorValue[BUMPER] == 1){
				avoidObstacle(bLeftOrRight);
			}

			currentColorL = SensorValue[ColorL];
			currentColorR = SensorValue[ColorR];

			// Lets ignore 0 reading from lineleader. It is tired and needs milliseconds to catch its breath.
			while(sensor == 0) {
				sensor =LLreadResult(LLEADER);
			}
			LLreadSensorRaw(LLEADER, llVal);

			//// load in line leader values
			//for (int i = 0; i < 7; i+=2) {
			//	//displayTextLine(i/2+2, "S%2d: %3d S%2d: %3d", 7-i, (short) llVal[i], 6-i, (short) llVal[i+1]);
			//	sens[7-i]=(short) llVal[i];
			//	sens[6-i]=(short) llVal[i+1];
			//}

			//binarize sensors; if value read is <30 assume it is dark; else it is white
			for (int i=0; i<8; i++){
				if (llVal[i]<=30){
					sens[7-i]=1;
					} else {
					sens[7-i]=0;
				}
			}

			int error = 0; //Error for line follow motor control

			// sens
			// black_begin, black_end are the locations of the first and last black points on the sensor
			// configuration is symmetric if black_begin = 9-black_end
			// if symmetric - go fast with max speed
			// if black_begin + black_end > 9 turn left
			// if black_begin + black_end < 9 turn right
			// error = black_begin + black_end - 9
			// the greater the error, the more the turn
			// left_motor_speed = base_speed - error * k
			// right_motor_speed = base_speed + error * k
			black_begin = -1;
			for (int i=0; i<8; i++){
				if (sens[i]==1){

					black_begin=i+1;
					break;
				}
			}

			black_end = -1;
			for (int i = 7; i>=0; i--){
				if (sens[i]==1){
					black_end=i+1;
					break;
				}
			}

			//	1 2 3 4 5 6 7 8  	==> Sensors
			//	0 0 0 1 1 0 0 0		==> middle 2 sensors are seeing black
			if (black_begin != -1 && black_end != -1) {
				error = black_begin + black_end - 9;
				whitespace = 0;
				} else {
				error = 0;
				whitespace = 1;
			}
			//calculate slow moving average error
			weighted_error = weighted_error * error_lambda + error;
			normalized_error = weighted_error * (1 - error_lambda);
			if (debug) writeDebugStreamLine(" Normalized weighted error, %f", normalized_error);
			if (ignore_Color_Sensor_While_Turning == 0) {
				if (currentColorR == GREEN_COLOR && currentColorL == GREEN_COLOR) {// see green on both sides, turn back.
					turnBack();
				}
				if (currentColorR == GREEN_COLOR)  {
					seen_green_right = FORGET_COLOR_SENSOR;
					//keep_line_following = false;
				}
				if (currentColorL == GREEN_COLOR) {
					seen_green_left = FORGET_COLOR_SENSOR;
					//keep_line_following = false;
				}
			}
			{//turn block - if a green sighting is
			 // followed by a black on the same side, turn
				//keep_line_following = false;
				if ((seen_green_right > 0) && (currentColorR == BLACK_COLOR)) {
					// hard right
					seen_green_right = 0;
					//greenTurn(true);
					if (turn_time == 0) {
						turn = INJECTION;
						turn_time = INJECTION_TIME;
					}
					ignore_Color_Sensor_While_Turning = IGNORE_COLOR_SENSOR_FOR_SOMETIME;

					} else if ((seen_green_left > 0)  && (currentColorL == BLACK_COLOR)) {
					seen_green_left = 0;
					//greenTurn(false);
					if (turn_time == 0) {
						turn = - INJECTION;
						turn_time = INJECTION_TIME;
					}
					ignore_Color_Sensor_While_Turning = IGNORE_COLOR_SENSOR_FOR_SOMETIME;
					//# hard left
					} else if ((seen_green_left == 0) && (seen_green_right == 0)) {
					//# go straight do nothing because its a gap
					} else {
					//# reached the end of a t junction
					//seen_green_left = 0;
					//seen_green_right = 0;
				}
			}

			//bias the error higher (for right) or lower (for left) turn
			error = error + turn;

			//use accumulated errors to avoid noise
			PrintArrayError();
			error_accum = (error + SumArrayError()) * 2/(arrayErrorsLength + 1); // simple average of the past errors, mulitply by a constant for gain

			//PID logic - quadratic slows down robot when its turning sharply
			//		float left_motor_speed  = baseSpeed  + error_accum*gain  - (c * error_accum*error_accum);
			//		float right_motor_speed = baseSpeed  - error_accum*gain - (c * error_accum*error_accum);
			if (whitespace == 0) {
				left_motor_speed  = baseSpeed + error_accum*gain - 1.2*baseSpeed*error_accum*error_accum/(14*14);
				right_motor_speed = baseSpeed - error_accum*gain - 1.2*baseSpeed*error_accum*error_accum/(14*14);
				} else { // if we are currently not on the 					    // line, rely on the very slow 						    // PID - to keep turning if we 						    // were already turning
				left_motor_speed  = baseSpeed + normalized_error*2*gain ;
				right_motor_speed = baseSpeed - normalized_error*2*gain ;
			}
			if (debug) writeDebugStreamLine("error  %d, %d, %d, %d  %d, %d",   black_begin, black_end, error, error_accum, left_motor_speed, right_motor_speed );
			if (debug) writeDebugStreamLine("colors  %d, %d, %d, %d  %d, %d, %d, %d",   currentColorL, currentColorR, seen_green_left, seen_green_right, ignore_Color_Sensor_While_Turning, turn, INJECTION, FORGET_COLOR_SENSOR );
			motor[motorC]= left_motor_speed;
			motor[motorB]= right_motor_speed;

			ShiftArrayError();
			arrayErrors[0] = error;
			//count down various counters that keep track
			// of when we last saw green on right or left
			// side, when to read color sensor, and how
			// long to keep the turn
			if (seen_green_right >= 1 ) {
				seen_green_right = seen_green_right - 1;
			}
			if (seen_green_left >= 1) {
				seen_green_left = seen_green_left - 1;
			}
			if (ignore_Color_Sensor_While_Turning >= 1) {
				ignore_Color_Sensor_While_Turning = ignore_Color_Sensor_While_Turning - 1;
			}
			if (turn_time >= 1){
				turn_time = turn_time - 1;
				if (turn_time == 0) {
					turn  = 0;
				}
			}
		}
	}
	//end room
	//doRescueRoom();
}

//this routine avoids obstacles
void avoidObstacle(bool bLeftOrRight) {
	bool leftSawLine = false;
	bool rightSawLine = false;
	if (bLeftOrRight==true) {
		motor[motorC]= +20; //right
		motor[motorB]= -20;
		sleep(3000);
		motor[motorC]= +20; //straight
		motor[motorB]= +20;
		sleep(4000);
		motor[motorC]= -20; //left
		motor[motorB]= +20;
		sleep(3300);
		motor[motorC]= +20; //straight
		motor[motorB]= +20;
		sleep(4000); //5000
		motor[motorC]= -20; //left
		motor[motorB]= +20;
		sleep(3000);
		motor[motorC]= +20; //straight
		motor[motorB]= +20;
		//sleep(500);

		while (! leftSawLine || ! rightSawLine ) {							//Makes sure the robot has passed a black line before realligning
			if (SensorValue[ColorL] == 1) leftSawLine = true;
			if (SensorValue[ColorR] == 1) rightSawLine = true;
		}

		motor[motorC]= +20; //straight
		motor[motorB]= +20;
		sleep(2000);
		motor[motorC]= +20; // right - assist to align to ine
		motor[motorB]= -20;
		sleep(3000);
		motor[motorC]= 0; // stop
		motor[motorB]= 0;

		}	else {

		motor[motorC]= -20; //left
		motor[motorB]= +20;
		sleep(3000);
		motor[motorC]= +20; //straight
		motor[motorB]= +20;
		sleep(4000);
		motor[motorC]= +20; //right
		motor[motorB]= -20;
		sleep(3300);
		motor[motorC]= +20; //straight
		motor[motorB]= +20;
		sleep(4000); //5000
		motor[motorC]= +20; //right
		motor[motorB]= -20;
		sleep(3000);
		motor[motorC]= +20; //straight
		motor[motorB]= +20;
	//	sleep(500);

		while (! leftSawLine || ! rightSawLine ) {
			if (SensorValue[ColorL] == 1) leftSawLine = true;
			if (SensorValue[ColorR] == 1) rightSawLine = true;
		}

		motor[motorC]= +20; //straight
		motor[motorB]= +20;
		sleep(2000);
		motor[motorC]= -20; // left - assist to align to ine
		motor[motorB]= +20;
		sleep(3000);
		motor[motorC]= 0; // stop
		motor[motorB]= 0;

	}
}
//shift errors for PID
void ShiftArrayError() {
	memmove(&arrayErrors[1], &arrayErrors[0], ((sizeof(arrayErrors)/sizeof(int)) -1)*sizeof(int)); //memmove(pToBuffer, pFromBuffer, nNumbOfBytes);
}
//add errors for PID
int SumArrayError() {
	int sumResult=0;
	for (int i = 0; i < (sizeof(arrayErrors)/sizeof(int)); i++) {
		sumResult += arrayErrors[i];
	}
	return sumResult;
}

void ResetArrayError() {
	memset(arrayErrors, 0, (sizeof(arrayErrors)/sizeof(int))*sizeof(int));
}

void PrintArrayError() {
	for (int i = 0; i < arrayErrorsLength; i++) {
		writeDebugStreamLine("arrayErrors = %d", arrayErrors[i]);
	}
}

// Saw green on both sides and have to turn back.
void turnBack() {
	// Spin turn for until approx 180deg.
	motor[motorC]= +20;
	motor[motorB]= -20;
	sleep(8500);
	motor[motorC]= 0;
	motor[motorB]= 0;
}