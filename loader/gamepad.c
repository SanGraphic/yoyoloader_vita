/* gamepad.c -- Gamepad code
 *
 * Copyright (C) 2022 Rinnegatamante
 * Copyright (C) 2022 JohnnyonFlame
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.	See the LICENSE file for details.
 */

// This file has been hugely derived from https://github.com/JohnnyonFlame/droidports/tree/master/ports/gmloader
#include <vitasdk.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>

#include "main.h"
#include "so_util.h"

#define NUM_BUTTONS 16

#define IS_AXIS_BOUNDS (axis >= 0 && axis < 4)
#define IS_BTN_BOUNDS (btn >= 0 && btn < NUM_BUTTONS)
#define IS_CONTROLLER_BOUNDS (id >= 0 && id < 4)

#define ANALOG_DEADZONE 30

extern so_module yoyoloader_mod;
extern int platTarget;
extern char fake_env[0x1000];

int (*YYGetInt32) (void *args, int idx);
void (*YYCreateString) (retval_t *ret, const char *str);
int (*CreateDsMap) (int a1, char *type, int a3, int a4, char *desc, char *type2, double id, int a8);
void (*GamepadUpdateM) ();
void (*ProcessVirtualKeys) ();
void (*IO_UpdateM) ();
void (*CreateAsynEventWithDSMap) (int dsMap, int a2);
void (*CheckKeyPressed) (retval_t *ret, void *self, void *other, int argc, retval_t *args);
int (*Java_com_yoyogames_runner_RunnerJNILib_KeyEvent) (void *env, int a2, int state, int key_code, int unicode_key, int source);
extern void (*Function_Add)(const char *name, intptr_t func, int argc, char ret);
int *g_MousePosX, *g_MousePosY, *g_DoMouseButton;

enum {
	DISABLED,
	CAMERA_MODE,
	CURSOR_MODE
};

int has_click_emulation = DISABLED;
int analog_as_mouse = DISABLED;
int analog_as_keys = DISABLED;
int has_kb_mapping = DISABLED;
char keyboard_mapping[NUM_BUTTONS];
int is_key_pressed[NUM_BUTTONS] = {0};
enum {
	CROSS_BTN,
	CIRCLE_BTN,
	SQUARE_BTN,
	TRIANGLE_BTN,
	L1_BTN,
	R1_BTN,
	L2_BTN,
	R2_BTN,
	SELECT_BTN,
	START_BTN,
	L3_BTN,
	R3_BTN,
	UP_BTN,
	DOWN_BTN,
	LEFT_BTN,
	RIGHT_BTN,
	LEFT_ANALOG,
	RIGHT_ANALOG,
	UNK_BTN = 0xFF
};

typedef struct {
	char key_name[32];
	char key_value;
} key_map;

key_map special_keys[] = {
	{"ENTER", 13},
	{"SHIFT", 16},
	{"CTRL", 17},
	{"ALT", 18},
	{"ESC", 27},
	{"BACKSPACE", 8},
	{"TAB", 9},
	{"PRINTSCREEN", 44},
	{"LEFT", 21},
	{"RIGHT", 22},
	{"UP", 19},
	{"DOWN", 20},
	{"HOME", 36},
	{"END", 35},
	{"DEL", 46},
	{"INS", 45},
	{"PAGEUP", 33},
	{"PAGEDOWN", 34},
	{"F1", 112},
	{"F2", 113},
	{"F3", 114},
	{"F4", 115},
	{"F5", 116},
	{"F6", 117},
	{"F7", 118},
	{"F8", 119},
	{"F9", 120},
	{"F10", 121},
	{"F11", 122},
	{"F12", 123},
	{"NUMPAD0", 96},
	{"NUMPAD1", 97},
	{"NUMPAD2", 98},
	{"NUMPAD3", 99},
	{"NUMPAD4", 100},
	{"NUMPAD5", 101},
	{"NUMPAD6", 102},
	{"NUMPAD7", 103},
	{"NUMPAD8", 104},
	{"NUMPAD9", 105}
};

typedef enum GamepadButtonState {
	GAMEPAD_BUTTON_STATE_UP = -1,
	GAMEPAD_BUTTON_STATE_NEUTRAL = 0,
	GAMEPAD_BUTTON_STATE_HELD = 1,
	GAMEPAD_BUTTON_STATE_DOWN = 2
} GamepadButtonState;

typedef struct Gamepad {
	int is_available; 
	double buttons[16];
	double deadzone;
	double axis[4];
} Gamepad;

Gamepad yoyo_gamepads[4];

int is_gamepad_connected(int id) {
	return yoyo_gamepads[id].is_available;
}

void GetPlatformInstance(void *self, int n, retval_t *args) {
	args[0].kind = VALUE_REAL;
	
	switch (platTarget) {
	case 1: // Windows
		args[0].rvalue.val = 0.0f;
		break;
	case 2: // PS4
		args[0].rvalue.val = 14.0f;
		break;
	default: // Android
		args[0].rvalue.val = 4.0f;
		break;
	}
}

void gamepad_is_supported(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	ret->kind = VALUE_BOOL;
	ret->rvalue.val = 1.0f;
}

void gamepad_get_device_count(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	ret->kind = VALUE_REAL;
	ret->rvalue.val = 4.0f;
}

void gamepad_is_connected(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	ret->kind = VALUE_REAL;
	int id = (int)args[0].rvalue.val;
	
	if (!IS_CONTROLLER_BOUNDS) {
		ret->rvalue.val = 0.0f;
		return;
	}
	
	ret->rvalue.val = (yoyo_gamepads[id].is_available) ? 1.0f : 0.0f;
}

void gamepad_get_description(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	static const char kName[] = "Xbox 360 Controller (XInput STANDARD GAMEPAD)";
	if (YYCreateString) {
		YYCreateString(ret, kName);
	} else {
		ref_t *ref = malloc(sizeof(ref_t));
		*ref = (ref_t){
			.m_refCount = 1,
			.m_size = strlen(kName),
			.m_thing = strdup(kName)
		};
		ret->kind = VALUE_STRING;
		ret->rvalue.str = ref;
	}
}

void gamepad_get_guid(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	static const char kGuid[] = "030000005e0400008e02000010010000";
	if (YYCreateString) {
		YYCreateString(ret, kGuid);
	} else {
		ref_t *ref = malloc(sizeof(ref_t));
		*ref = (ref_t){
			.m_refCount = 1,
			.m_size = strlen(kGuid),
			.m_thing = strdup(kGuid)
		};
		ret->kind = VALUE_STRING;
		ret->rvalue.str = ref;
	}
}

void gamepad_get_mapping(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	static const char kMapping[] = "030000005e0400008e02000014010000,Xbox 360 Controller (XInput STANDARD GAMEPAD),a:b0,b:b1,back:b6,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b8,leftshoulder:b4,leftstick:b9,lefttrigger:a2,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b10,righttrigger:a5,rightx:a3,righty:a4,start:b7,x:b2,y:b3";
	if (YYCreateString) {
		YYCreateString(ret, kMapping);
	} else {
		ref_t *ref = malloc(sizeof(ref_t));
		*ref = (ref_t){
			.m_refCount = 1,
			.m_size = strlen(kMapping),
			.m_thing = strdup(kMapping)
		};
		ret->kind = VALUE_STRING;
		ret->rvalue.str = ref;
	}
}

void gamepad_get_button_threshold(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	ret->kind = VALUE_REAL;
	ret->rvalue.val = 0.5f;
}

void gamepad_set_button_threshold(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
}

void gamepad_axis_count(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	ret->kind = VALUE_REAL;
	ret->rvalue.val = 4.f;
}

void gamepad_set_axis_deadzone(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	int id = (int)args[0].rvalue.val;
	double deadzone = args[1].rvalue.val;
	
	if (!IS_CONTROLLER_BOUNDS) {
		return;
	}

	yoyo_gamepads[id].deadzone = deadzone;
}

void gamepad_get_axis_deadzone(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	ret->kind = VALUE_REAL;
	int id = (int)args[0].rvalue.val;
	
	if (!IS_CONTROLLER_BOUNDS) {
		ret->rvalue.val = 0.0f;
		return;
	}
 
	ret->rvalue.val = yoyo_gamepads[id].deadzone;
}

static inline int get_rvalue_int(retval_t *args, int idx) {
	if (YYGetInt32) {
		return YYGetInt32(args, idx);
	}
	retval_t *r = &args[idx];
	if (r->kind == VALUE_INT32)
		return r->rvalue.v32;
	if (r->kind == VALUE_INT64)
		return (int)r->rvalue.v64;
	if (r->kind == VALUE_BOOL)
		return r->rvalue.v32 ? 1 : 0;
	return (int)r->rvalue.val;
}

static inline int translate_button(retval_t *args, int idx) {
	int v = get_rvalue_int(args, idx);
	if (v >= 32769 && v < 32769 + NUM_BUTTONS)
		v -= 32769;
	return v;
}

static inline int translate_axis(retval_t *args, int idx) {
	int v = get_rvalue_int(args, idx);
	if (v >= 32785 && v < 32785 + 4)
		v -= 32785;
	return v;
}

void gamepad_axis_value(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	ret->kind = VALUE_REAL;
	int id = get_rvalue_int(args, 0);
	int axis = translate_axis(args, 1);
	
	if (id < 0 || id >= 4 || !IS_AXIS_BOUNDS) {
		ret->rvalue.val = 0.0f;
		return;
	}

	ret->rvalue.val = yoyo_gamepads[id].axis[axis];
	if (fabs(ret->rvalue.val) < yoyo_gamepads[id].deadzone)
		ret->rvalue.val = 0.0f;
}

static bool step_held[4][NUM_BUTTONS] = {0};
static bool step_pressed[4][NUM_BUTTONS] = {0};
static bool step_released[4][NUM_BUTTONS] = {0};
static bool prev_down[4][NUM_BUTTONS] = {0};
static uint8_t pending_press[4][NUM_BUTTONS] = {0};
static uint8_t pending_release[4][NUM_BUTTONS] = {0};
static int touch_active = 0;
static int touch_pressed = 0;
static int gh_step_tick_count = 0;

void gamepad_button_check(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	prof_gml_pad_count++;
	ret->kind = VALUE_REAL;
	int id = get_rvalue_int(args, 0);
	int btn = translate_button(args, 1);
	
	if (id < 0 || id >= 4 || !IS_BTN_BOUNDS) {
		ret->rvalue.val = 0.0f;
		return;
	}

	ret->rvalue.val = step_held[id][btn] ? 1.0f : 0.0f;
}

void gamepad_button_check_pressed(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	prof_gml_pad_count++;
	ret->kind = VALUE_REAL;
	int id = get_rvalue_int(args, 0);
	int btn = translate_button(args, 1);
	
	if (id < 0 || id >= 4 || !IS_BTN_BOUNDS) {
		ret->rvalue.val = 0.0f;
		return;
	}

	ret->rvalue.val = step_pressed[id][btn] ? 1.0f : 0.0f;
}

void gamepad_button_check_released(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	prof_gml_pad_count++;
	ret->kind = VALUE_REAL;
	int id = get_rvalue_int(args, 0);
	int btn = translate_button(args, 1);
	
	if (id < 0 || id >= 4 || !IS_BTN_BOUNDS) {
		ret->rvalue.val = 0.0f;
		return;
	}

	ret->rvalue.val = step_released[id][btn] ? 1.0f : 0.0f;
}

void gamepad_button_count(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	ret->kind = VALUE_REAL;
	ret->rvalue.val = 16.f;
}

void gamepad_button_value(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	gamepad_button_check(ret, self, other, argc, args);
}

void gamepad_set_vibration(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
}

void gamepad_set_colour(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
}

static bool is_key_down_synthetic(int k) {
	if (k == 32 || k == 13) return step_held[0][CROSS_BTN];
	if (k == 16 || k == 160 || k == 161 || k == 17 || k == 67 || k == 90)
		return (step_held[0][CIRCLE_BTN] || step_held[0][L1_BTN] || step_held[0][L2_BTN]);
	if (k == 82) return step_held[0][SQUARE_BTN];
	if (k == 69 || k == 75) return step_held[0][TRIANGLE_BTN];
	if (k == 27 || k == 80) return step_held[0][START_BTN];
	if (k == 9) return step_held[0][SELECT_BTN];
	if (k == 87 || k == 38) return (step_held[0][UP_BTN] || yoyo_gamepads[0].axis[1] < -0.25);
	if (k == 83 || k == 40) return (step_held[0][DOWN_BTN] || yoyo_gamepads[0].axis[1] > 0.25);
	if (k == 65 || k == 37) return (step_held[0][LEFT_BTN] || yoyo_gamepads[0].axis[0] < -0.25);
	if (k == 68 || k == 39) return (step_held[0][RIGHT_BTN] || yoyo_gamepads[0].axis[0] > 0.25);
	if (k == 1) {
		for (int b = 0; b < NUM_BUTTONS; b++) {
			if (step_held[0][b]) return true;
		}
	}
	return false;
}

static bool is_key_pressed_synthetic(int k) {
	if (k == 32 || k == 13) return step_pressed[0][CROSS_BTN];
	if (k == 16 || k == 160 || k == 161 || k == 17 || k == 67 || k == 90)
		return (step_pressed[0][CIRCLE_BTN] || step_pressed[0][L1_BTN] || step_pressed[0][L2_BTN]);
	if (k == 82) return step_pressed[0][SQUARE_BTN];
	if (k == 69 || k == 75) return step_pressed[0][TRIANGLE_BTN];
	if (k == 27 || k == 80) return step_pressed[0][START_BTN];
	if (k == 9) return step_pressed[0][SELECT_BTN];
	if (k == 87 || k == 38) return step_pressed[0][UP_BTN];
	if (k == 83 || k == 40) return step_pressed[0][DOWN_BTN];
	if (k == 65 || k == 37) return step_pressed[0][LEFT_BTN];
	if (k == 68 || k == 39) return step_pressed[0][RIGHT_BTN];
	if (k == 1) {
		for (int b = 0; b < NUM_BUTTONS; b++) {
			if (step_pressed[0][b]) return true;
		}
	}
	return false;
}

static bool is_key_released_synthetic(int k) {
	if (k == 32 || k == 13) return step_released[0][CROSS_BTN];
	if (k == 16 || k == 160 || k == 161 || k == 17 || k == 67 || k == 90)
		return (step_released[0][CIRCLE_BTN] || step_released[0][L1_BTN] || step_released[0][L2_BTN]);
	if (k == 82) return step_released[0][SQUARE_BTN];
	if (k == 69 || k == 75) return step_released[0][TRIANGLE_BTN];
	if (k == 27 || k == 80) return step_released[0][START_BTN];
	if (k == 9) return step_released[0][SELECT_BTN];
	return false;
}

static void gml_keyboard_check(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	prof_gml_kb_count++;
	ret->kind = VALUE_REAL;
	int k = (argc > 0) ? get_rvalue_int(args, 0) : 1;
	ret->rvalue.val = is_key_down_synthetic(k) ? 1.0f : 0.0f;
}

static void gml_keyboard_check_pressed(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	prof_gml_kb_count++;
	ret->kind = VALUE_REAL;
	int k = (argc > 0) ? get_rvalue_int(args, 0) : 1;
	ret->rvalue.val = is_key_pressed_synthetic(k) ? 1.0f : 0.0f;
}

static void gml_keyboard_check_released(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	prof_gml_kb_count++;
	ret->kind = VALUE_REAL;
	int k = (argc > 0) ? get_rvalue_int(args, 0) : 1;
	ret->rvalue.val = is_key_released_synthetic(k) ? 1.0f : 0.0f;
}

static void gml_mouse_check_button(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	prof_gml_mouse_count++;
	ret->kind = VALUE_REAL;
	int b = (argc > 0) ? get_rvalue_int(args, 0) : 1;
	if (b == 1 || b == -1) {
		ret->rvalue.val = (step_held[0][R1_BTN] || step_held[0][R2_BTN] || touch_active) ? 1.0f : 0.0f;
	} else {
		ret->rvalue.val = 0.0f;
	}
}

static void gml_mouse_check_button_pressed(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	prof_gml_mouse_count++;
	ret->kind = VALUE_REAL;
	int b = (argc > 0) ? get_rvalue_int(args, 0) : 1;
	if (b == 1 || b == -1) {
		ret->rvalue.val = (step_pressed[0][R1_BTN] || step_pressed[0][R2_BTN] || touch_pressed) ? 1.0f : 0.0f;
	} else {
		ret->rvalue.val = 0.0f;
	}
}

static void gml_mouse_check_button_released(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	prof_gml_mouse_count++;
	ret->kind = VALUE_REAL;
	int b = (argc > 0) ? get_rvalue_int(args, 0) : 1;
	if (b == 1 || b == -1) {
		ret->rvalue.val = (step_released[0][R1_BTN] || step_released[0][R2_BTN]) ? 1.0f : 0.0f;
	} else {
		ret->rvalue.val = 0.0f;
	}
}

void mouse_set(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	*g_MousePosX = YYGetInt32(args, 0);
    *g_MousePosY = YYGetInt32(args, 1);
}

void mouse_get_x(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	ret->kind = VALUE_REAL;
	ret->rvalue.val = *g_MousePosX;
}

void mouse_get_y(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	ret->kind = VALUE_REAL;
	ret->rvalue.val = *g_MousePosY;
}

int GamePadCheck(int startup) {
	yoyo_gamepads[0].is_available = 1;
	return 1;
}

void GamePadRestart() {
	if (CreateDsMap && CreateAsynEventWithDSMap) {
		int dsMap = CreateDsMap(2, "event_type", 0, 0, "gamepad discovered", "pad_index", 0.0, 0);
		CreateAsynEventWithDSMap(dsMap, 0x4B);
	}
}

void GamePadUpdate() {
	// Synced directly inside IO_Update
}

void map_key(int key, const char *val) {
}

void map_analog(int idx, const char *val) {
}

extern void profiler_tick_frame(void);
extern uint32_t prof_gml_kb_count;
extern uint32_t prof_gml_mouse_count;
extern uint32_t prof_gml_pad_count;

void IO_Update() {
	gh_step_tick_count++;
	profiler_tick_frame();

	IO_UpdateM();
	GamepadUpdateM();
	ProcessVirtualKeys();

	// 1. Poll PS Vita physical hardware state
	SceCtrlData pad;
	sceCtrlPeekBufferPositiveExt2(0, &pad, 1);

	uint8_t cur_raw[NUM_BUTTONS] = {
		(pad.buttons & SCE_CTRL_CROSS) ? 1 : 0,    // 0: gp_face1 (Cross / Jump)
		(pad.buttons & SCE_CTRL_CIRCLE) ? 1 : 0,   // 1: gp_face2 (Circle / Slide)
		(pad.buttons & SCE_CTRL_SQUARE) ? 1 : 0,   // 2: gp_face3 (Square / Reload)
		(pad.buttons & SCE_CTRL_TRIANGLE) ? 1 : 0, // 3: gp_face4 (Triangle / Melee)
		(pad.buttons & SCE_CTRL_L1) ? 1 : 0,       // 4: gp_shoulderl (L1 / Slide)
		(pad.buttons & SCE_CTRL_R1) ? 1 : 0,       // 5: gp_shoulderr (R1 / Shoot)
		(pad.buttons & (SCE_CTRL_L1 | SCE_CTRL_L2)) ? 1 : 0, // 6: gp_shoulderlb (L2 / Slide)
		(pad.buttons & (SCE_CTRL_R1 | SCE_CTRL_R2)) ? 1 : 0, // 7: gp_shoulderrb (R2 / Shoot)
		(pad.buttons & (SCE_CTRL_SELECT | SCE_CTRL_TRIANGLE)) ? 1 : 0, // 8: gp_select / Melee
		(pad.buttons & SCE_CTRL_START) ? 1 : 0,    // 9: gp_start (Pause)
		(pad.buttons & SCE_CTRL_L3) ? 1 : 0,       // 10: gp_stickl
		(pad.buttons & SCE_CTRL_R3) ? 1 : 0,       // 11: gp_stickr
		(pad.buttons & SCE_CTRL_UP) ? 1 : 0,       // 12: gp_padu
		(pad.buttons & SCE_CTRL_DOWN) ? 1 : 0,     // 13: gp_padd
		(pad.buttons & SCE_CTRL_LEFT) ? 1 : 0,     // 14: gp_padl
		(pad.buttons & SCE_CTRL_RIGHT) ? 1 : 0     // 15: gp_padr
	};

#ifndef STANDALONE_MODE
	if (cur_raw[SELECT_BTN] && cur_raw[START_BTN] && cur_raw[L1_BTN] && cur_raw[R1_BTN])
		sceAppMgrLoadExec("app0:eboot.bin", NULL, NULL);
#endif

	// Rearpad touch for L2/R2
	SceTouchData touchBack;
	sceTouchPeek(SCE_TOUCH_PORT_BACK, &touchBack, 1);
	for (int j = 0; j < touchBack.reportNum; j++) {
		int x = touchBack.report[j].x;
		int y = touchBack.report[j].y;
		if (x > 960) {
			if (y > 544) cur_raw[R3_BTN] = 1;
			else { cur_raw[R2_BTN] = 1; cur_raw[R1_BTN] = 1; }
		} else {
			if (y > 544) cur_raw[L3_BTN] = 1;
			else { cur_raw[L2_BTN] = 1; cur_raw[L1_BTN] = 1; }
		}
	}

	// 2. Step-stable edge latching for GML step
	for (int b = 0; b < NUM_BUTTONS; b++) {
		bool cur_down = (cur_raw[b] != 0);
		step_pressed[0][b]  = (cur_down && !prev_down[0][b]);
		step_held[0][b]     = cur_down;
		step_released[0][b] = (!cur_down && prev_down[0][b]);
		prev_down[0][b]     = cur_down;
		yoyo_gamepads[0].buttons[b] = cur_down ? 1.0 : 0.0;
	}

	// 3. Analog sticks
	double lx = (double)((int)pad.lx - 128) / 128.0;
	double ly = (double)((int)pad.ly - 128) / 128.0;
	double rx = (double)((int)pad.rx - 128) / 128.0;
	double ry = (double)((int)pad.ry - 128) / 128.0;
	yoyo_gamepads[0].axis[0] = (fabs(lx) < 0.12) ? 0.0 : lx;
	yoyo_gamepads[0].axis[1] = (fabs(ly) < 0.12) ? 0.0 : ly;
	yoyo_gamepads[0].axis[2] = (fabs(rx) < 0.12) ? 0.0 : rx;
	yoyo_gamepads[0].axis[3] = (fabs(ry) < 0.12) ? 0.0 : ry;

	// 4. Front touch screen for menus
	SceTouchData touchFront;
	sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touchFront, 1);
	if (touchFront.reportNum > 0) {
		float tx = (float)touchFront.report[0].x * (float)SCREEN_W / 1920.0f;
		float ty = (float)touchFront.report[0].y * (float)SCREEN_H / 1088.0f;
		*g_MousePosX = (int)tx;
		*g_MousePosY = (int)ty;
		*g_DoMouseButton = 1;
		touch_pressed = (touch_active == 0) ? 1 : 0;
		touch_active = 1;
	} else {
		if (touch_active) {
			*g_DoMouseButton = 0;
			touch_active = 0;
			touch_pressed = 0;
		}
	}

	// 5. Gamepad discovery event dispatch during startup (first 240 steps / ~4s)
	if (CreateDsMap && CreateAsynEventWithDSMap && gh_step_tick_count <= 240) {
		int dsMap = CreateDsMap(2, "event_type", 0, 0, "gamepad discovered", "pad_index", 0.0, 0);
		CreateAsynEventWithDSMap(dsMap, 0x4B);
	}
}

void patch_gamepad(const char *game_name) {
	IO_UpdateM = (void *)so_symbol(&yoyoloader_mod, "_Z10IO_UpdateMv");
	GamepadUpdateM = (void *)so_symbol(&yoyoloader_mod, "_Z14GamepadUpdateMv");
	ProcessVirtualKeys = (void *)so_symbol(&yoyoloader_mod, "_Z18ProcessVirtualKeysv");
	CheckKeyPressed = (void *)so_symbol(&yoyoloader_mod, "_Z17F_CheckKeyPressedR6RValueP9CInstanceS2_iPS_");
	CreateDsMap = (void *)so_symbol(&yoyoloader_mod, "_Z11CreateDsMapiz");
	CreateAsynEventWithDSMap = (void *)so_symbol(&yoyoloader_mod, "_Z24CreateAsynEventWithDSMapii");
	Java_com_yoyogames_runner_RunnerJNILib_KeyEvent = (void *)so_symbol(&yoyoloader_mod, "Java_com_yoyogames_runner_RunnerJNILib_KeyEvent");

	yoyo_gamepads[0].is_available = 1;
	yoyo_gamepads[0].deadzone = 0.12;
	has_click_emulation = 1;
	
	Function_Add("gamepad_is_supported", (intptr_t)gamepad_is_supported, 0, 1);
	Function_Add("gamepad_get_device_count", (intptr_t)gamepad_get_device_count, 0, 1);
	Function_Add("gamepad_is_connected", (intptr_t)gamepad_is_connected, 1, 1);
	Function_Add("gamepad_get_description", (intptr_t)gamepad_get_description, 1, 1);
	Function_Add("gamepad_get_guid", (intptr_t)gamepad_get_guid, 1, 1);
	Function_Add("gamepad_get_mapping", (intptr_t)gamepad_get_mapping, 1, 1);
	Function_Add("gamepad_get_button_threshold", (intptr_t)gamepad_get_button_threshold, 1, 1);
	Function_Add("gamepad_set_button_threshold", (intptr_t)gamepad_set_button_threshold, 2, 1);
	Function_Add("gamepad_get_axis_deadzone", (intptr_t)gamepad_get_axis_deadzone, 1, 1);
	Function_Add("gamepad_set_axis_deadzone", (intptr_t)gamepad_set_axis_deadzone, 2, 1);
	Function_Add("gamepad_button_count", (intptr_t)gamepad_button_count, 1, 1);
	Function_Add("gamepad_button_check", (intptr_t)gamepad_button_check, 2, 1);
	Function_Add("gamepad_button_check_pressed", (intptr_t)gamepad_button_check_pressed, 2, 1);
	Function_Add("gamepad_button_check_released", (intptr_t)gamepad_button_check_released, 2, 1);
	Function_Add("gamepad_button_value", (intptr_t)gamepad_button_value, 2, 1);
	Function_Add("gamepad_axis_count", (intptr_t)gamepad_axis_count, 1, 1);
	Function_Add("gamepad_axis_value", (intptr_t)gamepad_axis_value, 2, 1);
	Function_Add("gamepad_set_vibration", (intptr_t)gamepad_set_vibration, 3, 1);
	Function_Add("gamepad_set_color", (intptr_t)gamepad_set_colour, 2, 1);
	Function_Add("gamepad_set_colour", (intptr_t)gamepad_set_colour, 2, 1);
	hook_addr(so_symbol(&yoyoloader_mod, "_Z14GamePadRestartv"), (intptr_t)GamePadRestart);
	hook_addr(so_symbol(&yoyoloader_mod, "_Z9IO_Updatev"), (intptr_t)IO_Update);
	
	YYGetInt32 = (void *)so_symbol(&yoyoloader_mod, "_Z10YYGetInt32PK6RValuei");
	YYCreateString = (void *)so_symbol(&yoyoloader_mod, "_Z14YYCreateStringP6RValuePKc");
	g_MousePosX = (int *)so_symbol(&yoyoloader_mod, "g_MousePosX");
	g_MousePosY = (int *)so_symbol(&yoyoloader_mod, "g_MousePosY");
	g_DoMouseButton = (int *)so_symbol(&yoyoloader_mod, "g_DoMouseButton");
	
	Function_Add("display_mouse_set", (intptr_t)mouse_set, 2, 0);
	Function_Add("window_mouse_set", (intptr_t)mouse_set, 2, 0);
	Function_Add("display_mouse_get_x", (intptr_t)mouse_get_x, 1, 0);
	Function_Add("display_mouse_get_y", (intptr_t)mouse_get_y, 1, 0);
	Function_Add("window_mouse_get_x", (intptr_t)mouse_get_x, 1, 0);
	Function_Add("window_mouse_get_y", (intptr_t)mouse_get_y, 1, 0);
	Function_Add("keyboard_check", (intptr_t)gml_keyboard_check, 1, 0);
	Function_Add("keyboard_check_pressed", (intptr_t)gml_keyboard_check_pressed, 1, 0);
	Function_Add("keyboard_check_released", (intptr_t)gml_keyboard_check_released, 1, 0);
	Function_Add("keyboard_check_direct", (intptr_t)gml_keyboard_check, 1, 0);
	Function_Add("mouse_check_button", (intptr_t)gml_mouse_check_button, 1, 0);
	Function_Add("mouse_check_button_pressed", (intptr_t)gml_mouse_check_button_pressed, 1, 0);
	Function_Add("mouse_check_button_released", (intptr_t)gml_mouse_check_button_released, 1, 0);
}
