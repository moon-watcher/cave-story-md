#include "tsc.h"

#include <genesis.h>
#include "audio.h"
#include "player.h"
#include "entity.h"
#include "stage.h"
#include "input.h"
#include "camera.h"
#include "resources.h"
#include "system.h"
#include "vdp_ext.h"
#include "tables.h"
#include "hud.h"
#include "window.h"
#include "effect.h"
#include "gamemode.h"
#include "sprite.h"
#include "sheet.h"

#ifdef KDB_TSC
#define logcmd(...) printf(__VA_ARGS__)
#else
#define logcmd(...) /**/
#endif

// Execution State
#define TSC_IDLE 0 // Not executing any events
#define TSC_RUNNING 1 // Executing event commands
#define TSC_WAITTIME 2 // Waiting on a timer before continuing
#define TSC_WAITINPUT 3 // Waiting for player to press C
#define TSC_PROMPT 4 // Prompting yes/no
#define TSC_TELEMENU 5 // Teleport menu
#define TSC_WAITGROUNDED 6 // Waiting for the player to touch the ground

#define HEAD_EVENT_COUNT 14 // There are exactly 14
#define MAX_EVENTS 106 // Largest is ArmsItem with 106

// TSC Commands
#define FIRST_CMD 0x80
#define CMD_MSG 0x80
#define CMD_MS2 0x81
#define CMD_MS3 0x82
#define CMD_CLO 0x83
#define CMD_CLR 0x84
#define CMD_NUM 0x85
#define CMD_GIT 0x86
#define CMD_FAC 0x87
#define CMD_CAT 0x88
#define CMD_SAT 0x89
#define CMD_TUR 0x8a
#define CMD_YNJ 0x8b
#define CMD_END 0x8c
#define CMD_EVE 0x8d
#define CMD_TRA 0x8e
#define CMD_INI 0x8f
#define CMD_LDP 0x90
#define CMD_ESC 0x91
#define CMD_CMU 0x92
#define CMD_FMU 0x93
#define CMD_RMU 0x94
#define CMD_SOU 0x95
#define CMD_SPS 0x96
#define CMD_CPS 0x97
#define CMD_SSS 0x98
#define CMD_CSS 0x99
#define CMD_NOD 0x9a
#define CMD_WAI 0x9b
#define CMD_WAS 0x9c
#define CMD_MM0 0x9d
#define CMD_MOV 0x9e
#define CMD_MYB 0x9f
#define CMD_MYD 0xa0
#define CMD_UNI 0xa1
#define CMD_UNJ 0xa2
#define CMD_KEY 0xa3
#define CMD_PRI 0xa4
#define CMD_FRE 0xa5
#define CMD_HMC 0xa6
#define CMD_SMC 0xa7
#define CMD_LI_ADD 0xa8
#define CMD_ML_ADD 0xa9
#define CMD_ANP 0xaa
#define CMD_CNP 0xab
#define CMD_MNP 0xac
#define CMD_DNA 0xad
#define CMD_DNP 0xae
#define CMD_INP 0xaf
#define CMD_SNP 0xb0
#define CMD_BOA 0xb1
#define CMD_BSL 0xb2
#define CMD_NCJ 0xb3
#define CMD_ECJ 0xb4
#define CMD_AE_ADD 0xb5
#define CMD_ZAM 0xb6
#define CMD_AM_ADD 0xb7
#define CMD_AM_SUB 0xb8
#define CMD_TAM 0xb9
#define CMD_AMJ 0xba
#define CMD_EQ_ADD 0xbb
#define CMD_EQ_SUB 0xbc
#define CMD_IT_ADD 0xbd
#define CMD_IT_SUB 0xbe
#define CMD_ITJ 0xbf
#define CMD_FL_ADD 0xc0
#define CMD_FL_SUB 0xc1
#define CMD_FLJ 0xc2
#define CMD_SK_ADD 0xc3
#define CMD_SK_SUB 0xc4
#define CMD_SKJ 0xc5
#define CMD_FOB 0xc6
#define CMD_FOM 0xc7
#define CMD_FON 0xc8
#define CMD_QUA 0xc9
#define CMD_FAI 0xca
#define CMD_FAO 0xcb
#define CMD_FLA 0xcc
#define CMD_MLP 0xcd
#define CMD_MNA 0xce
#define CMD_CMP 0xcf
#define CMD_MP_ADD 0xd0
#define CMD_MPJ 0xd1
#define CMD_CRE 0xd2
#define CMD_SIL 0xd3
#define CMD_CIL 0xd4
#define CMD_SLP 0xd5
#define CMD_PS_ADD 0xd6
#define CMD_SVP 0xd7
#define CMD_STC 0xd8
#define CMD_XX1 0xd9
#define CMD_SMP 0xda
#define LAST_CMD 0xda

typedef struct {
	u16 number;
	const u8 *data;
} Event;

// Array of pointers to each event in the current TSC
Event headEvents[HEAD_EVENT_COUNT];
Event stageEvents[MAX_EVENTS];

const u8 *curCommand = NULL;

u16 waitTime;

u16 promptJump = 0;

u8 teleMenuSlotCount = 0;
u16 teleMenuEvent[8];
u8 teleMenuSelection = 0;
u8 teleMenuSheet = NOSHEET;
VDPSprite teleMenuSprite[8];

u16 bossMaxHealth;
u16 bossHealth;

u16 lastAmmoNum = 0;

u8 tsc_load(Event *eventList, const u8 *TSC, u8 max);

void tsc_show_boss_health();
void tsc_hide_boss_health();
void tsc_show_teleport_menu();

u8 execute_command();
u8 tsc_read_byte();
u16 tsc_read_word();

void tsc_init() {
	tscState = TSC_IDLE;
	VDP_loadTileSet(&TS_Window, TILE_WINDOWINDEX, TRUE);
	const u8 *TSC = TSC_Head;
	tsc_load(headEvents, TSC, HEAD_EVENT_COUNT);
}

void tsc_load_stage(u8 id) {
	if(id == 255) {
		const u8 *TSC = TSC_ArmsItem;
		tscEventCount = tsc_load(stageEvents, TSC, MAX_EVENTS);
	} else {
		const u8 *TSC = stage_info[id].TSC;
		tscEventCount = tsc_load(stageEvents, TSC, MAX_EVENTS);
	}
}

u8 tsc_load(Event *eventList, const u8 *TSC, u8 max) {
	// First byte of TSC is the number of events
	u8 eventCount = TSC[0];
	// Make sure it isn't more than can be handled
	if(eventCount > max) {
		char str[32] = "Too many events: ";
		intToStr(eventCount, &str[17], 1);
		SYS_die(str);
	}
	// Step through ROM data until finding all the events
	u8 loadedEvents = 0;
	for(u16 i = 1; loadedEvents < eventCount; i++) {
		// The event marker is a word 0xFFFF
		if(TSC[i] == 0xFF && TSC[i+1] == 0xFF) {
			eventList[loadedEvents].number = TSC[i+2]+(TSC[i+3]<<8);
			eventList[loadedEvents].data = &TSC[i+4];
			loadedEvents++;
			i += 3;
		}
	}
	return loadedEvents;
}

void tsc_call_event(u16 number) {
	if(number < 50) {
		for(u8 i = 0; i < HEAD_EVENT_COUNT; i++) {
			if(headEvents[i].number == number) {
				tscState = TSC_RUNNING;
				curCommand = headEvents[i].data;
				return;
			}
		}
	} else {
		for(u8 i = 0; i < tscEventCount; i++) {
			if(stageEvents[i].number == number) {
				tscState = TSC_RUNNING;
				curCommand = stageEvents[i].data;
				return;
			}
		}
	}
}

u8 tsc_update() {
	switch(tscState) {
		case TSC_IDLE: break; // Nothing to update
		case TSC_RUNNING:
		{
			for(;;) {
				u8 result = execute_command();
				if(result > 0) return result - 1;
			}
		}
		break;
		case TSC_WAITTIME:
		{
			waitTime--;
			if(paused) {
				waitTime = 0;
			// Check the wait time again to prevent underflowing
			} else if(waitTime > 0 && joy_down(BUTTON_A)) {
				// Fast forward while holding A, update active entities a second time
				// to double their movement speed (unless <PRI is set)
				waitTime--;
				if(!gameFrozen) entities_update();
			}
			if(waitTime == 0) tscState = TSC_RUNNING;
		}
		break;
		case TSC_WAITINPUT:
		{
			if(joy_pressed(BUTTON_C) || (joystate & BUTTON_A)) {
				tscState = TSC_RUNNING;
			}
		}
		break;
		case TSC_PROMPT:
		{
			if(window_prompt_update()) {
				if(!window_prompt_answer()) tsc_call_event(promptJump);
				tscState = TSC_RUNNING;
			}
		}
		break;
		case TSC_TELEMENU:
		{
			if(joy_pressed(BUTTON_C)) {
				tsc_call_event(teleMenuEvent[teleMenuSelection]);
				tscState = TSC_RUNNING;
			} else if(joy_pressed(BUTTON_B)) { // Cancel
				tscState = TSC_RUNNING;
			} else if(joy_pressed(BUTTON_LEFT)) {
				sprite_index(teleMenuSprite[teleMenuSelection], 
						sheets[teleMenuSheet].index + teleMenuSelection*16);
				if(teleMenuSelection == 0) {
					teleMenuSelection = teleMenuSlotCount - 1;
				} else {
					teleMenuSelection--;
				}
				sound_play(SND_MENU_MOVE, 5);
			} else if(joy_pressed(BUTTON_RIGHT)) {
				sprite_index(teleMenuSprite[teleMenuSelection], 
						sheets[teleMenuSheet].index + teleMenuSelection*16);
				if(teleMenuSelection == teleMenuSlotCount - 1) {
					teleMenuSelection = 0;
				} else {
					teleMenuSelection++;
				}
				sound_play(SND_MENU_MOVE, 5);
			} else { // Doing nothing, blink cursor
				sprite_index(teleMenuSprite[teleMenuSelection], 
						sheets[teleMenuSheet].index + teleMenuSelection*16 + 8);
			}
			sprite_addq(teleMenuSprite, teleMenuSlotCount);
		}
		break;
		case TSC_WAITGROUNDED:
		{
			if(player.grounded) tscState = TSC_RUNNING;
		}
		break;
		default:
		{
			puts("Invalid TSC State");
			tscState = TSC_IDLE;
		}
		break;
	}
	return 0;
}

void tsc_show_boss_health() {
	showingBossHealth = TRUE;
	VDP_setWindowPos(0, 254);
	VDP_drawTextWindow("Boss[        ]  ", 24, 26);
	VDP_drawTextWindow("                ", 24, 27);
	// Face tiles are unused during boss battles, upload the tiles there
	VDP_loadTileData(TS_HudBar.tiles, TILE_FACEINDEX, 9, TRUE);
}

void tsc_update_boss_health() {
	if(bossEntity == NULL) {
		tsc_hide_boss_health();
	} else if(bossEntity->health > bossMaxHealth) {
		VDP_drawTextWindow("?HP>MHP?", 29, 26);
	} else if(bossHealth != bossEntity->health) {
		bossHealth = bossEntity->health;
		if(bossHealth == 0) {
			// Boss is dead hide the bar
			tsc_hide_boss_health();
			return;
		}
		u16 hp = bossHealth, inc = bossMaxHealth / 8, i;
		// Draw filled tiles
		for(i = 0; i < 8 && hp > inc; i++) {
			hp -= inc;
			VDP_setTileMapXY(PLAN_WINDOW, 29 + i, 26, 
				TILE_ATTR_FULL(PAL0, 0, 0, 0, TILE_FACEINDEX + 8));
		}
		// If boss health is full no need to go any further
		if(bossHealth == bossMaxHealth) return;
		// Draw a partial filled tile
		i++;
		VDP_setTileMapXY(PLAN_WINDOW, 29 + i, 26, 
			TILE_ATTR_FULL(PAL0, 0, 0, 0, TILE_FACEINDEX + hp / 8));
		// draw empty tiles
		for(; i < 8; i++) {
			VDP_setTileMapXY(PLAN_WINDOW, 29 + i, 26, 
				TILE_ATTR_FULL(PAL0, 0, 0, 0, TILE_FACEINDEX));
		}
	}
}

void tsc_hide_boss_health() {
	showingBossHealth = FALSE;
	VDP_setWindowPos(0, 0);
}

void tsc_show_teleport_menu() {
	teleMenuSlotCount = 0;
	SHEET_FIND(teleMenuSheet, SHEET_TELE);
	for(u8 i = 0; i < 8; i++) {
		if(teleportEvent[i] == 0) continue;
		teleMenuEvent[teleMenuSlotCount] = teleportEvent[i];
		teleMenuSprite[teleMenuSlotCount] = (VDPSprite) {
			.x = 160 + i*40, .y = 224,
			.size = SPRITE_SIZE(4, 2),
			.attribut = TILE_ATTR_FULL(PAL0,1,0,0,sheets[teleMenuSheet].index + (i-1)*16)
		};
		teleMenuSlotCount++;
	}
	if(teleMenuSlotCount > 0) {
		tscState = TSC_TELEMENU;
	} else {
		tscState = TSC_RUNNING; // Don't bother with the menu if we can't teleport
	}
}

u8 execute_command() {
	u16 args[4];
	u8 cmd = tsc_read_byte();
	if(cmd >= 0x80) {
		switch(cmd) {
		case CMD_MSG: // Display message box (bottom - visible)
			logcmd("<MSG");
			window_open(0);
			break;
		case CMD_MS2: // Display message box (top - invisible)
			logcmd("<MS2");
			window_open(0);
			break;
		case CMD_MS3: // Display message box (top - visible)
			logcmd("<MS3");
			window_open(0);
			break;
		case CMD_CLO: // Close message box
			logcmd("<CLO");
			window_close();
			break;
		case CMD_CLR: // Clear message box
			logcmd("<CLR");
			window_clear();
			break;
		case CMD_NUM: // Show number (1) in message box
			args[0] = tsc_read_word();
			logcmd("<NUM:%hu", args[0]);
			{
				char str[12];
				intToStr(lastAmmoNum, str, 1);
				for(u8 i = 0; str[i] != 0 && i < 12; i++) {
					window_draw_char(str[i]);
				}
			}
			break;
		case CMD_GIT: // Display item (1) in message box
			args[0] = tsc_read_word();
			logcmd("<GIT:%hu", args[0]);
			if(args[0] >= 1000) {
				window_show_item(args[0] - 1000);
			} else {
				window_show_weapon(args[0]);
			}
			break;
		case CMD_FAC: // Display face (1) in message box
			args[0] = tsc_read_word();
			logcmd("<FAC:%hu", args[0]);
			window_set_face(args[0], TRUE);
			break;
		case CMD_CAT: // All 3 of these display text instantly
			logcmd("<CAT");
			window_set_textmode(TM_LINE);
			break;
		case CMD_SAT:
			logcmd("<SAT");
			window_set_textmode(TM_LINE);
			break;
		case CMD_TUR:
			logcmd("<TUR");
			window_set_textmode(TM_ALL);
			break;
		case CMD_YNJ: // Prompt Yes/No and jump to event (1) if No
			args[0] = tsc_read_word();
			logcmd("<YNJ:%hu", args[0]);
			promptJump = args[0];
			window_prompt_open();
			tscState = TSC_PROMPT;
			return 1;
		case CMD_END: // End the event
			logcmd("<END");
			tscState = TSC_IDLE;
			if(!paused) {
				gameFrozen = FALSE;
				window_set_face(0, FALSE);
				window_close();
				controlsLocked = FALSE;
				hud_show();
			}
			return 1;
		case CMD_EVE: // Jump to event (1)
			args[0] = tsc_read_word();
			logcmd("<EVE:%hu", args[0]);
			tsc_call_event(args[0]);
			break;
		case CMD_TRA: // Teleport to stage (1), run event (2), coords (3),(4)
			args[0] = tsc_read_word();
			args[1] = tsc_read_word();
			args[2] = tsc_read_word();
			args[3] = tsc_read_word();
			logcmd("<TRA:%hu:%hu:%hu:%hu", args[0], args[1], args[2], args[3]);
			player.x = block_to_sub(args[2]) + pixel_to_sub(8);
			player.y = block_to_sub(args[3]) + pixel_to_sub(8);
			player.x_speed = 0;
			player.y_speed = 0;
			player.grounded = FALSE;
			gameFrozen = FALSE;
			window_set_face(0, FALSE);
			window_close();
			stage_load(args[0]);
			tsc_call_event(args[1]);
			return 1;
		case CMD_INI: // Start from beginning (try again without save data)
			logcmd("<INI");
			return 4;
		case CMD_ESC: // Restart the game
			logcmd("<ESC");
			return 2;
		case CMD_LDP: // Reload save file (try again)
			logcmd("<LDP");
			return 3;
		case CMD_CMU: // Play song (1), stop if 0
			args[0] = tsc_read_word();
			logcmd("<CMU:%hu", args[0]);
			song_play(args[0]);
			break;
		case CMD_FMU: // Fade out music (we just stop for now)
			logcmd("<FMU");
			song_stop();
			break;
		case CMD_RMU: // Resume previously playing music
			logcmd("<RMU");
			song_resume();
			break;
		case CMD_SOU: // Play sound (1)
			args[0] = tsc_read_word();
			logcmd("<SOU:%hu", args[0]);
			sound_play(args[0], 5);
			break;
		case CMD_SPS: // TODO: Persistent sounds, skip for now
			logcmd("<SPS");
			break;
		case CMD_CPS:
			logcmd("<CPS");
			break;
		case CMD_SSS:
			args[0] = tsc_read_word();
			logcmd("<SSS:%hu", args[0]);
			break;
		case CMD_CSS:
			logcmd("<CSS");
			break;
		case CMD_NOD: // Wait for player input
			logcmd("<NOD");
			tscState = TSC_WAITINPUT;
			return 1;
		case CMD_WAI: // Wait (1) frames
			args[0] = tsc_read_word();
			logcmd("<WAI:%hu", args[0]);
			tscState = TSC_WAITTIME;
			waitTime = TIME(args[0]);
			return 1;
		case CMD_WAS: // Wait for player to hit the ground
			logcmd("<WAS");
			tscState = TSC_WAITGROUNDED;
			return 1;
		case CMD_MM0: // Halt player movement
			logcmd("<MM0");
			player.x_speed = 0;
			player.y_speed = 0;
			break;
		case CMD_MOV: // Move player to coordinates (1),(2)
			args[0] = tsc_read_word();
			args[1] = tsc_read_word();
			logcmd("<MOV:%hu:%hu", args[0], args[1]);
			player.x = block_to_sub(args[0]) + pixel_to_sub(8);
			player.y = block_to_sub(args[1]) + pixel_to_sub(8);
			player.grounded = FALSE;
			break;
		case CMD_MYB: // Bounce player in direction (1)
			args[0] = tsc_read_word();
			logcmd("<MYB:%hu", args[0]);
			if(args[0] == 0) { // Left
				player.x_speed = pixel_to_sub(1);
			} else if(args[0] == 2) { // Right
				player.x_speed = pixel_to_sub(-1);
			}
			player.y_speed = pixel_to_sub(-2);
			break;
		case CMD_MYD: // Change direction to (1)
			args[0] = tsc_read_word();
			logcmd("<MYD:%hu", args[0]);
			if(args[0] == 0) { // Left
				player.dir = 0;
			} else if(args[0] == 2) { // Right
				player.dir = 1;
			}
			break;
		case CMD_UNI: // Change movement type to (1)
			args[0] = tsc_read_word();
			logcmd("<UNI:%hu", args[0]);
			playerMoveMode = args[0];
			break;
		case CMD_UNJ: // If movement type is (1) jump to event (2)
			args[0] = tsc_read_word();
			args[1] = tsc_read_word();
			logcmd("<UNJ:%hu:%hu", args[0], args[1]);
			if(playerMoveMode == args[0]) {
				tsc_call_event(args[1]);
			}
			break;
		case CMD_KEY: // Lock controls and hide the HUD
			logcmd("<KEY");
			controlsLocked = TRUE;
			hud_hide();
			gameFrozen = FALSE;
			break;
		case CMD_PRI: // Lock controls
			logcmd("<PRI");
			controlsLocked = TRUE;
			gameFrozen = TRUE;
			break;
		case CMD_FRE: // Unlock controls
			logcmd("<FRE");
			controlsLocked = FALSE;
			hud_show();
			gameFrozen = FALSE;
			break;
		case CMD_HMC: // Hide player character
			logcmd("<HMC");
			player.hidden = TRUE;
			break;
		case CMD_SMC: // Show player character
			logcmd("<SMC");
			player.hidden = FALSE;
			break;
		case CMD_LI_ADD: // Restore health by (1)
			args[0] = tsc_read_word();
			logcmd("<LI+:%hu", args[0]);
			player_heal(args[0]);
			break;
		case CMD_ML_ADD: // Increase max health by (1)
			args[0] = tsc_read_word();
			logcmd("<ML+:%hu", args[0]);
			player_maxhealth_increase(args[0]);
			break;
		case CMD_ANP: // Give all entities of event (1) script state (2) with direction (3)
			args[0] = tsc_read_word();
			args[1] = tsc_read_word();
			args[2] = tsc_read_word();
			logcmd("<ANP:%hu:%hu:%hu", args[0], args[1], args[2]);
			entities_set_state(args[0], args[1], args[2] > 0);
			break;
		case CMD_CNP: // Change all entities of event (1) to type (2) with direction (3)
			args[0] = tsc_read_word();
			args[1] = tsc_read_word();
			args[2] = tsc_read_word();
			logcmd("<CNP:%hu:%hu:%hu", args[0], args[1], args[2]);
			entities_replace(args[0], args[1], args[2] > 0, 0);
			break;
		case CMD_MNP: // Move entity of event (1) to (2),(3) with direction (4)
			args[0] = tsc_read_word();
			args[1] = tsc_read_word();
			args[2] = tsc_read_word();
			args[3] = tsc_read_word();
			logcmd("<MNP:%hu:%hu:%hu:%hu", args[0], args[1], args[2], args[3]);
			entities_move(args[0], args[1], args[2], args[3] > 0);
			break;
		case CMD_DNA: // Delete all entities of type (1)
			args[0] = tsc_read_word();
			logcmd("<DNA:%hu", args[0]);
			entities_clear_by_type(args[0]);
			break;
		case CMD_DNP: // Delete all entities with event # (1)
			args[0] = tsc_read_word();
			logcmd("<DNP:%hu", args[0]);
			entities_clear_by_event(args[0]);
			break;
		// Change entity of event (1) to type (2) with direction (3) and set option 2 flag
		case CMD_INP:
			args[0] = tsc_read_word();
			args[1] = tsc_read_word();
			args[2] = tsc_read_word();
			logcmd("<INP:%hu:%hu:%hu", args[0], args[1], args[2]);
			entities_replace(args[0], args[1], args[2] > 0, 0x1000);
			break;
		case CMD_SNP: // Create entity (1) at (2),(3) with direction (4)
		{
			args[0] = tsc_read_word();
			args[1] = tsc_read_word();
			args[2] = tsc_read_word();
			args[3] = tsc_read_word();
			logcmd("<SNP:%hu:%hu:%hu:%hu", args[0], args[1], args[2], args[3]);
			Entity *e = entity_create((args[1]<<CSF)*16+(8<<CSF), 
									  (args[2]<<CSF)*16+(8<<CSF), args[0], 0);
			e->dir = args[3] > 0;
		}
		break;
		case CMD_BOA: // Set boss state to (1)
			args[0] = tsc_read_word();
			logcmd("<BOA:%hu", args[0]);
			// The real cave story has the stage boss created at stage load in a dormant state.
			// NXEngine also does this, but I don't, instead waiting until the boss is used
			// In a <BOA command to create it. A bit hacky but it works.
			if(stageID == 0x0A && args[0] == 20) {
				// Omega in Sand Zone
				bossEntity = entity_create(0, 0, 360 + BOSS_OMEGA, 0);
				bossEntity->event = 210;
				bossEntity->state = 20;
			} else if(stageID == 0x27 && args[0] == 1) {
				// Monster X - #0301 <BOA0001
				bossEntity = entity_create(0, 0, 360 + BOSS_MONSTERX, 0);
				bossEntity->event = 1000;
				bossEntity->state = 1;
			} else if(stageID == 0x2F && args[0] == 200) {
				// Core
				bossEntity = entity_create(0, 0, 360 + BOSS_CORE, 0);
				bossEntity->event = 1000;
				bossEntity->state = 200;
			} else if(bossEntity) {
				bossEntity->state = args[0];
			}
			break;
		case CMD_BSL: // Start boss fight with entity (1)
			args[0] = tsc_read_word();
			logcmd("<BSL:%hu", args[0]);
			bossEntity = entity_find_by_event(args[0]);
			if(bossEntity) {
				bossMaxHealth = bossHealth = bossEntity->health;
				tsc_show_boss_health();
			}
			break;
		case CMD_NCJ: // If entity type (1) exists jump to event (2)
			args[0] = tsc_read_word();
			args[1] = tsc_read_word();
			logcmd("<NCJ:%hu:%hu", args[0], args[1]);
			if(entity_find_by_type(args[0])) tsc_call_event(args[1]);
			break;
		case CMD_ECJ: // If entity id (1) exists jump to event (2)
			args[0] = tsc_read_word();
			args[1] = tsc_read_word();
			logcmd("<ECJ:%hu:%hu", args[0], args[1]);
			if(entity_find_by_id(args[0])) tsc_call_event(args[1]);
			break;
		case CMD_AE_ADD: // Refill all weapon ammo
			logcmd("<AE+");
			player_refill_ammo();
			break;
		case CMD_ZAM: // Take away all weapons and ammo
			logcmd("<ZAM");
			player_take_allweapons();
			break;
		case CMD_AM_ADD: // Give weapon (1) and ammo (2)
			args[0] = tsc_read_word();
			args[1] = tsc_read_word();
			logcmd("<AM+:%hu:%hu", args[0], args[1]);
			player_give_weapon(args[0], args[1]);
			lastAmmoNum = args[1];
			break;
		case CMD_AM_SUB: // Remove weapon (1)
			args[0] = tsc_read_word();
			logcmd("<AM-:%hu", args[0]);
			player_take_weapon(args[0]);
			break;
		case CMD_TAM: // Trade weapon (1) for (2) with (3) max ammo
			args[0] = tsc_read_word();
			args[1] = tsc_read_word();
			args[2] = tsc_read_word();
			logcmd("<TAM:%hu:%hu:%hu", args[0], args[1], args[2]);
			player_trade_weapon(args[0], args[1], args[2]);
			break;
		case CMD_AMJ: // If player has weapon (1) jump to event (2)
			args[0] = tsc_read_word();
			args[1] = tsc_read_word();
			logcmd("<AMJ:%hu:%hu", args[0], args[1]);
			if(player_has_weapon(args[0])) tsc_call_event(args[1]);
			break;
			// These two equip commands actually give a flag between 1<<0 and 1<<8
		case CMD_EQ_ADD: // Equip item (1)
			args[0] = tsc_read_word();
			logcmd("<EQ+:%hu", args[0]);
			player_equip(args[0]);
			break;
		case CMD_EQ_SUB: // Remove equip (1)
			args[0] = tsc_read_word();
			logcmd("<EQ-:%hu", args[0]);
			player_unequip(args[0]);
			break;
		case CMD_IT_ADD: // Give item (1)
			args[0] = tsc_read_word();
			logcmd("<IT+:%hu", args[0]);
			player_give_item(args[0]);
			break;
		case CMD_IT_SUB: // Remove item (1)
			args[0] = tsc_read_word();
			logcmd("<IT-:%hu", args[0]);
			player_take_item(args[0]);
			break;
		case CMD_ITJ: // If player has item (1) jump to event (2)
			args[0] = tsc_read_word();
			args[1] = tsc_read_word();
			logcmd("<ITJ:%hu:%hu", args[0], args[1]);
			if(player_has_item(args[0])) tsc_call_event(args[1]);
			break;
		case CMD_FL_ADD: // Set flag (1)
			args[0] = tsc_read_word();
			logcmd("<FL+:%hu", args[0]);
			system_set_flag(args[0], TRUE);
			break;
		case CMD_FL_SUB: // Unset flag (1)
			args[0] = tsc_read_word();
			logcmd("<FL-:%hu", args[0]);
			system_set_flag(args[0], FALSE);
			break;
		case CMD_FLJ: // If flag (1) is TRUE jump to event (2)
			args[0] = tsc_read_word();
			args[1] = tsc_read_word();
			logcmd("<FLJ:%hu:%hu", args[0], args[1]);
			if(system_get_flag(args[0])) tsc_call_event(args[1]);
			break;
		case CMD_SK_ADD: // Set skip flag (1)
			args[0] = tsc_read_word();
			logcmd("<SK+:%hu", args[0]);
			system_set_skip_flag(args[0], TRUE);
			break;
		case CMD_SK_SUB: // Unset skip flag (1)
			args[0] = tsc_read_word();
			logcmd("<SK-:%hu", args[0]);
			system_set_skip_flag(args[0], FALSE);
			break;
		case CMD_SKJ: // If skip flag (1) is TRUE jump to event (2)
			args[0] = tsc_read_word();
			args[1] = tsc_read_word();
			logcmd("<SKJ:%hu:%hu", args[0], args[1]);
			if(system_get_skip_flag(args[0])) tsc_call_event(args[1]);
			break;
		case CMD_FOB: // Focus on boss (1) with (2) ticks
			logcmd("<FOB");
			if(bossEntity) camera.target = bossEntity;
			break;
		case CMD_FON: // Focus on NPC (1) with (2) ticks
			args[0] = tsc_read_word();
			args[1] = tsc_read_word();
			logcmd("<FON:%hu:%hu", args[0], args[1]);
			Entity *e = entity_find_by_event(args[0]);
			camera.target = e ? e : &player;
			break;
		case CMD_FOM: // Focus on player at (1) speed
			args[0] = tsc_read_word();
			logcmd("<FOM:%hu", args[0]);
			camera.target = &player;
			break;
		case CMD_QUA: // Shake camera for (1) frames
			args[0] = tsc_read_word();
			logcmd("<QUA:%hu", args[0]);
			camera_shake(args[0]);
			break;
		case CMD_FAI: // Fading, in direction (1)
			args[0] = tsc_read_word();
			logcmd("<FAI:%hu", args[0]);
			VDP_fadeTo(0, 63, VDP_getCachedPalette(), 20, TRUE);
			break;
		case CMD_FAO:
			args[0] = tsc_read_word();
			logcmd("<FAO:%hu", args[0]);
			VDP_fadeTo(0, 63, PAL_FadeOut, 20, FALSE);
			break;
		case CMD_FLA: // Flash screen white
			logcmd("<FLA");
			VDP_setPaletteColors(0, PAL_FullWhite, 64);
			VDP_fadeTo(0, 63, VDP_getCachedPalette(), 10, TRUE);
			break;
		case CMD_MLP: // Show the map
			logcmd("<MLP");
			do_map();
			break;
		case CMD_MNA: // Show stage name
			logcmd("<MNA");
			player_show_map_name(180);
			break;
		case CMD_CMP: // Change stage tile at (1),(2) to type (3)
			args[0] = tsc_read_word();
			args[1] = tsc_read_word();
			args[2] = tsc_read_word();
			logcmd("<CMP:%hu:%hu:%hu", args[0], args[1], args[2]);
			// When I crushed some larger tilesets to better fit VRAM I inadvertently broke
			// CMP for maps using those tilesets. Thankfully TSC instructions are not critical
			// code so I can put in this hacky section which fixes specific scripts
			if(stageID == 14) { // Mimiga Village Shack - when Balrog barges in
				stage_replace_block(args[0], args[1], 
					(args[2]==80 || args[2]==81 || args[2]==82) ? args[2]+32 : args[2]+19);
			} else {
				stage_replace_block(args[0], args[1], args[2]);
			}
			// Puff of smoke
			effect_create_smoke(block_to_pixel(args[0]) + 8, block_to_pixel(args[1]) + 8);
			break;
		// These two "Map Flag" commands were mentioned in TSC.txt but may not exist
		// At least NXEngine doesn't check for them. I keep them here just in case
		case CMD_MP_ADD: // Map flag (1)
			args[0] = tsc_read_word();
			logcmd("<MP+:%hu", args[0]);
			break;
		case CMD_MPJ: // If map flag (1) set jump to event (2)
			args[0] = tsc_read_word();
			args[1] = tsc_read_word();
			logcmd("<MPJ:%hu:%hu", args[0], args[1]);
			break;
		case CMD_CRE: // TODO: Show credits
			logcmd("<CRE");
			break;
		case CMD_SIL: // TODO: Show illustration (1) in the credits
			args[0] = tsc_read_word();
			logcmd("<SIL:%hu", args[0]);
			break;
		case CMD_CIL: // TODO: Clear illustration in the credits
			logcmd("<CIL");
			break;
		case CMD_SLP: // Show the teleporter menu
			logcmd("<SLP");
			tsc_show_teleport_menu();
			return 1;
		case CMD_PS_ADD: // Set teleporter slot (1) to event (2)
			args[0] = tsc_read_word();
			args[1] = tsc_read_word();
			logcmd("<PS+:%hu:%hu", args[0], args[1]);
			if(args[0] < 8) teleportEvent[args[0]] = args[1];
			break;
		case CMD_SVP: // Save
			logcmd("<SVP");
			system_save();
			break;
		case CMD_STC: // TODO: Save counter
			logcmd("<STC");
			break;
		case CMD_XX1: // TODO: Island effect
			args[0] = tsc_read_word();
			logcmd("<XX1:%hu", args[0]);
			break;
		case CMD_SMP: // Subtract 1 from tile index at position (1), (2)
			args[0] = tsc_read_word();
			args[1] = tsc_read_word();
			logcmd("<SMP:%hu:%hu", args[0], args[1]);
			stage_replace_block(args[0], args[1], stage_get_block(args[0], args[1]) - 1);
			break;
		default:
			break;
		}
	} else if(window_is_open()) {
		if(window_tick() || (joystate & BUTTON_A)) {
			window_draw_char(cmd);
		} else {
			curCommand -= 1;
			return 1;
		}
	}
	return 0;
}

u8 tsc_read_byte() {
	u8 byte = curCommand[0];
	curCommand++;
	return byte;
}

u16 tsc_read_word() {
	u16 word = curCommand[0]+(curCommand[1]<<8);
	curCommand += 2;
	return word;
}
