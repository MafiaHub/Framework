/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

// Taken directly from the Windows SDK

#define FW_KEY_LBUTTON                         0x01
#define FW_KEY_RBUTTON                         0x02
#define FW_KEY_CANCEL                          0x03
#define FW_KEY_MBUTTON                         0x04 /* NOT contiguous with L & RBUTTON */
#define FW_KEY_XBUTTON1                        0x05 /* NOT contiguous with L & RBUTTON */
#define FW_KEY_XBUTTON2                        0x06 /* NOT contiguous with L & RBUTTON */
#define FW_KEY_BACK                            0x08
#define FW_KEY_TAB                             0x09
#define FW_KEY_CLEAR                           0x0C
#define FW_KEY_RETURN                          0x0D
#define FW_KEY_SHIFT                           0x10
#define FW_KEY_CONTROL                         0x11
#define FW_KEY_MENU                            0x12
#define FW_KEY_PAUSE                           0x13
#define FW_KEY_CAPITAL                         0x14
#define FW_KEY_KANA                            0x15
#define FW_KEY_HANGEUL                         0x15 /* old name - should be here for compatibility */
#define FW_KEY_HANGUL                          0x15
#define FW_KEY_IME_ON                          0x16
#define FW_KEY_JUNJA                           0x17
#define FW_KEY_FINAL                           0x18
#define FW_KEY_HANJA                           0x19
#define FW_KEY_KANJI                           0x19
#define FW_KEY_IME_OFF                         0x1A
#define FW_KEY_ESCAPE                          0x1B
#define FW_KEY_CONVERT                         0x1C
#define FW_KEY_NONCONVERT                      0x1D
#define FW_KEY_ACCEPT                          0x1E
#define FW_KEY_MODECHANGE                      0x1F
#define FW_KEY_SPACE                           0x20
#define FW_KEY_PRIOR                           0x21
#define FW_KEY_NEXT                            0x22
#define FW_KEY_END                             0x23
#define FW_KEY_HOME                            0x24
#define FW_KEY_LEFT                            0x25
#define FW_KEY_UP                              0x26
#define FW_KEY_RIGHT                           0x27
#define FW_KEY_DOWN                            0x28
#define FW_KEY_SELECT                          0x29
#define FW_KEY_PRINT                           0x2A
#define FW_KEY_EXECUTE                         0x2B
#define FW_KEY_SNAPSHOT                        0x2C
#define FW_KEY_INSERT                          0x2D
#define FW_KEY_DELETE                          0x2E
#define FW_KEY_HELP                            0x2F
#define FW_KEY_LWIN                            0x5B
#define FW_KEY_RWIN                            0x5C
#define FW_KEY_APPS                            0x5D
#define FW_KEY_SLEEP                           0x5F
#define FW_KEY_NUMPAD0                         0x60
#define FW_KEY_NUMPAD1                         0x61
#define FW_KEY_NUMPAD2                         0x62
#define FW_KEY_NUMPAD3                         0x63
#define FW_KEY_NUMPAD4                         0x64
#define FW_KEY_NUMPAD5                         0x65
#define FW_KEY_NUMPAD6                         0x66
#define FW_KEY_NUMPAD7                         0x67
#define FW_KEY_NUMPAD8                         0x68
#define FW_KEY_NUMPAD9                         0x69
#define FW_KEY_MULTIPLY                        0x6A
#define FW_KEY_ADD                             0x6B
#define FW_KEY_SEPARATOR                       0x6C
#define FW_KEY_SUBTRACT                        0x6D
#define FW_KEY_DECIMAL                         0x6E
#define FW_KEY_DIVIDE                          0x6F
#define FW_KEY_F1                              0x70
#define FW_KEY_F2                              0x71
#define FW_KEY_F3                              0x72
#define FW_KEY_F4                              0x73
#define FW_KEY_F5                              0x74
#define FW_KEY_F6                              0x75
#define FW_KEY_F7                              0x76
#define FW_KEY_F8                              0x77
#define FW_KEY_F9                              0x78
#define FW_KEY_F10                             0x79
#define FW_KEY_F11                             0x7A
#define FW_KEY_F12                             0x7B
#define FW_KEY_F13                             0x7C
#define FW_KEY_F14                             0x7D
#define FW_KEY_F15                             0x7E
#define FW_KEY_F16                             0x7F
#define FW_KEY_F17                             0x80
#define FW_KEY_F18                             0x81
#define FW_KEY_F19                             0x82
#define FW_KEY_F20                             0x83
#define FW_KEY_F21                             0x84
#define FW_KEY_F22                             0x85
#define FW_KEY_F23                             0x86
#define FW_KEY_F24                             0x87
#define FW_KEY_NAVIGATION_VIEW                 0x88 // reserved
#define FW_KEY_NAVIGATION_MENU                 0x89 // reserved
#define FW_KEY_NAVIGATION_UP                   0x8A // reserved
#define FW_KEY_NAVIGATION_DOWN                 0x8B // reserved
#define FW_KEY_NAVIGATION_LEFT                 0x8C // reserved
#define FW_KEY_NAVIGATION_RIGHT                0x8D // reserved
#define FW_KEY_NAVIGATION_ACCEPT               0x8E // reserved
#define FW_KEY_NAVIGATION_CANCEL               0x8F // reserved
#define FW_KEY_NUMLOCK                         0x90
#define FW_KEY_SCROLL                          0x91
#define FW_KEY_OEM_NEC_EQUAL                   0x92 // '=' key on numpad
#define FW_KEY_OEM_FJ_JISHO                    0x92 // 'Dictionary' key
#define FW_KEY_OEM_FJ_MASSHOU                  0x93 // 'Unregister word' key
#define FW_KEY_OEM_FJ_TOUROKU                  0x94 // 'Register word' key
#define FW_KEY_OEM_FJ_LOYA                     0x95 // 'Left OYAYUBI' key
#define FW_KEY_OEM_FJ_ROYA                     0x96 // 'Right OYAYUBI' key
#define FW_KEY_LSHIFT                          0xA0
#define FW_KEY_RSHIFT                          0xA1
#define FW_KEY_LCONTROL                        0xA2
#define FW_KEY_RCONTROL                        0xA3
#define FW_KEY_LMENU                           0xA4
#define FW_KEY_RMENU                           0xA5
#define FW_KEY_BROWSER_BACK                    0xA6
#define FW_KEY_BROWSER_FORWARD                 0xA7
#define FW_KEY_BROWSER_REFRESH                 0xA8
#define FW_KEY_BROWSER_STOP                    0xA9
#define FW_KEY_BROWSER_SEARCH                  0xAA
#define FW_KEY_BROWSER_FAVORITES               0xAB
#define FW_KEY_BROWSER_HOME                    0xAC
#define FW_KEY_VOLUME_MUTE                     0xAD
#define FW_KEY_VOLUME_DOWN                     0xAE
#define FW_KEY_VOLUME_UP                       0xAF
#define FW_KEY_MEDIA_NEXT_TRACK                0xB0
#define FW_KEY_MEDIA_PREV_TRACK                0xB1
#define FW_KEY_MEDIA_STOP                      0xB2
#define FW_KEY_MEDIA_PLAY_PAUSE                0xB3
#define FW_KEY_LAUNCH_MAIL                     0xB4
#define FW_KEY_LAUNCH_MEDIA_SELECT             0xB5
#define FW_KEY_LAUNCH_APP1                     0xB6
#define FW_KEY_LAUNCH_APP2                     0xB7
#define FW_KEY_OEM_1                           0xBA // ';:' for US
#define FW_KEY_OEM_PLUS                        0xBB // '+' any country
#define FW_KEY_OEM_COMMA                       0xBC // ',' any country
#define FW_KEY_OEM_MINUS                       0xBD // '-' any country
#define FW_KEY_OEM_PERIOD                      0xBE // '.' any country
#define FW_KEY_OEM_2                           0xBF // '/?' for US
#define FW_KEY_OEM_3                           0xC0 // '`~' for US
#define FW_KEY_GAMEPAD_A                       0xC3 // reserved
#define FW_KEY_GAMEPAD_B                       0xC4 // reserved
#define FW_KEY_GAMEPAD_X                       0xC5 // reserved
#define FW_KEY_GAMEPAD_Y                       0xC6 // reserved
#define FW_KEY_GAMEPAD_RIGHT_SHOULDER          0xC7 // reserved
#define FW_KEY_GAMEPAD_LEFT_SHOULDER           0xC8 // reserved
#define FW_KEY_GAMEPAD_LEFT_TRIGGER            0xC9 // reserved
#define FW_KEY_GAMEPAD_RIGHT_TRIGGER           0xCA // reserved
#define FW_KEY_GAMEPAD_DPAD_UP                 0xCB // reserved
#define FW_KEY_GAMEPAD_DPAD_DOWN               0xCC // reserved
#define FW_KEY_GAMEPAD_DPAD_LEFT               0xCD // reserved
#define FW_KEY_GAMEPAD_DPAD_RIGHT              0xCE // reserved
#define FW_KEY_GAMEPAD_MENU                    0xCF // reserved
#define FW_KEY_GAMEPAD_VIEW                    0xD0 // reserved
#define FW_KEY_GAMEPAD_LEFT_THUMBSTICK_BUTTON  0xD1 // reserved
#define FW_KEY_GAMEPAD_RIGHT_THUMBSTICK_BUTTON 0xD2 // reserved
#define FW_KEY_GAMEPAD_LEFT_THUMBSTICK_UP      0xD3 // reserved
#define FW_KEY_GAMEPAD_LEFT_THUMBSTICK_DOWN    0xD4 // reserved
#define FW_KEY_GAMEPAD_LEFT_THUMBSTICK_RIGHT   0xD5 // reserved
#define FW_KEY_GAMEPAD_LEFT_THUMBSTICK_LEFT    0xD6 // reserved
#define FW_KEY_GAMEPAD_RIGHT_THUMBSTICK_UP     0xD7 // reserved
#define FW_KEY_GAMEPAD_RIGHT_THUMBSTICK_DOWN   0xD8 // reserved
#define FW_KEY_GAMEPAD_RIGHT_THUMBSTICK_RIGHT  0xD9 // reserved
#define FW_KEY_GAMEPAD_RIGHT_THUMBSTICK_LEFT   0xDA // reserved
#define FW_KEY_OEM_4                           0xDB //  '[{' for US
#define FW_KEY_OEM_5                           0xDC //  '\|' for US
#define FW_KEY_OEM_6                           0xDD //  ']}' for US
#define FW_KEY_OEM_7                           0xDE //  ''"' for US
#define FW_KEY_OEM_8                           0xDF
#define FW_KEY_OEM_AX                          0xE1 //  'AX' key on Japanese AX kbd
#define FW_KEY_OEM_102                         0xE2 //  "<>" or "\|" on RT 102-key kbd.
#define FW_KEY_ICO_HELP                        0xE3 //  Help key on ICO
#define FW_KEY_ICO_00                          0xE4 //  00 key on ICO
#define FW_KEY_PROCESSKEY                      0xE5
#define FW_KEY_ICO_CLEAR                       0xE6
#define FW_KEY_PACKET                          0xE7
#define FW_KEY_OEM_RESET                       0xE9
#define FW_KEY_OEM_JUMP                        0xEA
#define FW_KEY_OEM_PA1                         0xEB
#define FW_KEY_OEM_PA2                         0xEC
#define FW_KEY_OEM_PA3                         0xED
#define FW_KEY_OEM_WSCTRL                      0xEE
#define FW_KEY_OEM_CUSEL                       0xEF
#define FW_KEY_OEM_ATTN                        0xF0
#define FW_KEY_OEM_FINISH                      0xF1
#define FW_KEY_OEM_COPY                        0xF2
#define FW_KEY_OEM_AUTO                        0xF3
#define FW_KEY_OEM_ENLW                        0xF4
#define FW_KEY_OEM_BACKTAB                     0xF5
#define FW_KEY_ATTN                            0xF6
#define FW_KEY_CRSEL                           0xF7
#define FW_KEY_EXSEL                           0xF8
#define FW_KEY_EREOF                           0xF9
#define FW_KEY_PLAY                            0xFA
#define FW_KEY_ZOOM                            0xFB
#define FW_KEY_NONAME                          0xFC
#define FW_KEY_PA1                             0xFD
#define FW_KEY_OEM_CLEAR                       0xFE
