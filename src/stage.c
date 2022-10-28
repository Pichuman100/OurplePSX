/*
  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include "stage.h"

#include "mem.h"
#include "timer.h"
#include "audio.h"
#include "pad.h"
#include "main.h"
#include "random.h"
#include "network.h"
#include "mutil.h"
#include "debug.h"
#include "save.h"

#include "menu.h"
#include "pause.h"
#include "trans.h"
#include "loadscr.h"

#include "object/combo.h"
#include "object/splash.h"

//Stage constants
//#define STAGE_NOHUD //Disable the HUD

int note_x[8];
int note_y[8];

static const u16 note_key[] = {INPUT_LEFT, INPUT_DOWN, INPUT_UP, INPUT_RIGHT};
static const u8 note_anims[4][3] = {
	{CharAnim_Left,  CharAnim_LeftAlt,  PlayerAnim_LeftMiss},
	{CharAnim_Down,  CharAnim_DownAlt,  PlayerAnim_DownMiss},
	{CharAnim_Up,    CharAnim_UpAlt,    PlayerAnim_UpMiss},
	{CharAnim_Right, CharAnim_RightAlt, PlayerAnim_RightMiss},
};

//Stage definitions
boolean noteshake;
boolean show;
fixed_t fade;
fixed_t fade, fadespd;
static u32 Sounds[10];

#include "character/bf.h"
#include "character/gf.h"
#include "character/gfo.h"
#include "character/ourple.h"
#include "character/prange.h"
#include "character/henry.h"
#include "character/henrytv.h"
#include "character/bfp.h"
#include "character/henryp.h"
#include "character/sunny.h"
#include "character/matpat.h"
#include "character/lphone.h"
#include "character/fatjones.h"
#include "character/gphone.h"
#include "character/goldenf.h"
#include "character/fbear.h"
#include "character/springb.h"
#include "character/mark.h"
#include "character/freddy.h"
#include "character/gfreddy.h"
#include "character/foxy.h"
#include "character/salvage.h"
#include "character/cassidy.h"
#include "character/omc.h"
#include "character/plush.h"
#include "character/cc.h"
#include "character/abdul.h"
#include "character/cakebear.h"
#include "character/jackass.h"
#include "character/ngt.h"
#include "character/gino.h"
#include "character/strap.h"
#include "character/mangle.h"
#include "character/criminal.h"
#include "character/dad.h"

#include "stage/dummy.h"
#include "stage/storage.h"
#include "stage/jrs.h"
#include "stage/deskroom.h"
#include "stage/black.h"
#include "stage/week1.h"
#include "stage/officeg.h"
#include "stage/stage01.h"
#include "stage/officeb.h"
#include "stage/fnaf3.h"
#include "stage/flipside.h"
#include "stage/takecake.h"
#include "stage/ngroom.h"
#include "stage/black2.h"
#include "stage/samroom.h"

static const StageDef stage_defs[StageId_Max] = {
	#include "stagedef_disc1.h"
};

//Stage states
Stage stage;
Debug debug;

//Stage music functions
static void Stage_StartVocal(void)
{
	if (!(stage.flag & STAGE_FLAG_VOCAL_ACTIVE))
	{
		Audio_ChannelXA(stage.stage_def->music_channel);
		stage.flag |= STAGE_FLAG_VOCAL_ACTIVE;
	}
}

static void Stage_CutVocal(void)
{
	if (stage.flag & STAGE_FLAG_VOCAL_ACTIVE)
	{
		Audio_ChannelXA(stage.stage_def->music_channel + 1);
		stage.flag &= ~STAGE_FLAG_VOCAL_ACTIVE;
	}
}

boolean zooming;
//Stage camera functions
static void Stage_FocusCharacter(Character *ch, fixed_t div)
{
	//Use character focus settings to update target position and zoom
	stage.camera.tx = ch->x + ch->focus_x;
	stage.camera.ty = ch->y + ch->focus_y;
	if (!zooming)
		stage.camera.tz = ch->focus_zoom;
	stage.camera.td = div;
}

static void Stage_ScrollCamera(void)
{
	if (stage.prefs.debug)
		Debug_ScrollCamera();
	else 
	{
		if (stage.freecam)
		{
			if (pad_state.held & PAD_LEFT)
				stage.camera.x -= FIXED_DEC(2,1);
			if (pad_state.held & PAD_UP)
				stage.camera.y -= FIXED_DEC(2,1);
			if (pad_state.held & PAD_RIGHT)
				stage.camera.x += FIXED_DEC(2,1);
			if (pad_state.held & PAD_DOWN)
				stage.camera.y += FIXED_DEC(2,1);
			if (pad_state.held & PAD_TRIANGLE)
				stage.camera.zoom -= FIXED_DEC(1,100);
			if (pad_state.held & PAD_CROSS)
				stage.camera.zoom += FIXED_DEC(1,100);
		}
		else
		{
			//Get delta position
			fixed_t dx = stage.camera.tx - stage.camera.x;
			fixed_t dy = stage.camera.ty - stage.camera.y;
			fixed_t dz = stage.camera.tz - stage.camera.zoom;
			
			//Scroll based off current divisor
			stage.camera.x += FIXED_MUL(dx, stage.camera.td);
			stage.camera.y += FIXED_MUL(dy, stage.camera.td);
			stage.camera.zoom += FIXED_MUL(dz, stage.camera.td);
		}
	}
		
	//Update other camera stuff
	stage.camera.bzoom = FIXED_MUL(stage.camera.zoom, stage.bump);

}

//Stage section functions
static void Stage_ChangeBPM(u16 bpm, u16 step)
{
	//Update last BPM
	stage.last_bpm = bpm;
	
	//Update timing base
	if (stage.step_crochet)
		stage.time_base += FIXED_DIV(((fixed_t)step - stage.step_base) << FIXED_SHIFT, stage.step_crochet);
	stage.step_base = step;
	
	//Get new crochet and times
	stage.step_crochet = ((fixed_t)bpm << FIXED_SHIFT) * 8 / 240; //15/12/24
	stage.step_time = FIXED_DIV(FIXED_DEC(12,1), stage.step_crochet);
	
	//Get new crochet based values
	stage.early_safe = stage.late_safe = stage.step_crochet / 6; //10 frames
	stage.late_sus_safe = stage.late_safe;
	stage.early_sus_safe = stage.early_safe * 2 / 5;
}

static Section *Stage_GetPrevSection(Section *section)
{
	if (section > stage.sections)
		return section - 1;
	return NULL;
}

static u16 Stage_GetSectionStart(Section *section)
{
	Section *prev = Stage_GetPrevSection(section);
	if (prev == NULL)
		return 0;
	return prev->end;
}

//Section scroll structure
typedef struct
{
	fixed_t start;   //Seconds
	fixed_t length;  //Seconds
	u16 start_step;  //Sub-steps
	u16 length_step; //Sub-steps
	
	fixed_t size; //Note height
} SectionScroll;

static void Stage_GetSectionScroll(SectionScroll *scroll, Section *section)
{
	//Get BPM
	u16 bpm = section->flag & SECTION_FLAG_BPM_MASK;
	
	//Get section step info
	scroll->start_step = Stage_GetSectionStart(section);
	scroll->length_step = section->end - scroll->start_step;
	
	//Get section time length
	scroll->length = (scroll->length_step * FIXED_DEC(15,1) / 12) * 24 / bpm;
	
	//Get note height
	scroll->size = FIXED_MUL(stage.speed, scroll->length * (12 * 150) / scroll->length_step) + FIXED_UNIT;
}

//Note hit detection
static u8 Stage_HitNote(PlayerState *this, u8 type, fixed_t offset)
{
	//Get hit type
	if (offset < 0)
		offset = -offset;
	
	u8 hit_type;
	if (offset > stage.late_safe * 9 / 11)
		hit_type = 3; //SHIT
	else if (offset > stage.late_safe * 6 / 11)
		hit_type = 2; //BAD
	else if (offset > stage.late_safe * 3 / 11)
		hit_type = 1; //GOOD
	else
		hit_type = 0; //SICK
	
	//Increment combo and score
	this->combo++;
	
	static const s32 score_inc[] = {
		35, //SICK
		20, //GOOD
		10, //BAD
		 5, //SHIT
	};
	this->score += score_inc[hit_type];

	this->min_accuracy += 100;

	this->max_accuracy += 100 + (hit_type*25);

	this->refresh_accuracy = true;
	this->refresh_score = true;
	
	//Restore vocals and health
	Stage_StartVocal();
	this->health += 230;
	
	//Create combo object telling of our combo
	Obj_Combo *combo = Obj_Combo_New(
		this->character->focus_x,
		this->character->focus_y,
		hit_type,
		this->combo >= 10 ? this->combo : 0xFFFF
	);
	if (combo != NULL)
		ObjectList_Add(&stage.objlist_fg, (Object*)combo);
	
	//Create note splashes if SICK
	if (hit_type == 0)
	{
		for (int i = 0; i < 3; i++)
		{
			//Create splash object
			Obj_Splash *splash = Obj_Splash_New(
				note_x[type],
				note_y[type] * (stage.prefs.downscroll ? -1 : 1),
				type & 0x3
			);
			if (splash != NULL)
				ObjectList_Add(&stage.objlist_splash, (Object*)splash);
		}
	}
	
	return hit_type;
}

static void Stage_MissNote(PlayerState *this)
{
	this->max_accuracy += 150;
	this->refresh_accuracy = true;
	this->miss += 1;
	this->refresh_miss = true;

	if (this->combo)
	{
		//Kill combo
		this->combo = 0;
		
		//Create combo object telling of our lost combo
		Obj_Combo *combo = Obj_Combo_New(
			this->character->focus_x,
			this->character->focus_y,
			0xFF,
			0
		);
		if (combo != NULL)
			ObjectList_Add(&stage.objlist_fg, (Object*)combo);
	}
}

static void Stage_NoteCheck(PlayerState *this, u8 type)
{
	//Perform note check
	for (Note *note = stage.cur_note;; note++)
	{
		if (!(note->type & NOTE_FLAG_MINE))
		{
			//Check if note can be hit
			fixed_t note_fp = (fixed_t)note->pos << FIXED_SHIFT;
			if (note_fp - stage.early_safe > stage.note_scroll)
				break;
			if (note_fp + stage.late_safe < stage.note_scroll)
				continue;
			if ((note->type & NOTE_FLAG_HIT) || (note->type & (NOTE_FLAG_OPPONENT | 0x3)) != type || (note->type & NOTE_FLAG_SUSTAIN))
				continue;
			
			//Hit the note
			note->type |= NOTE_FLAG_HIT;

		   this->character->set_anim(this->character, note_anims[type & 0x3][(note->type & NOTE_FLAG_ALT_ANIM) != 0]);

			u8 hit_type = Stage_HitNote(this, type, stage.note_scroll - note_fp);
			this->arrow_hitan[type & 0x3] = stage.step_time;
			(void)hit_type;
			return;
		}
		else
		{
			//Check if mine can be hit
			fixed_t note_fp = (fixed_t)note->pos << FIXED_SHIFT;
			if (note_fp - (stage.late_safe * 3 / 5) > stage.note_scroll)
				break;
			if (note_fp + (stage.late_safe * 2 / 5) < stage.note_scroll)
				continue;
			if ((note->type & NOTE_FLAG_HIT) || (note->type & (NOTE_FLAG_OPPONENT | 0x3)) != type || (note->type & NOTE_FLAG_SUSTAIN))
				continue;
			
			//Hit the mine
			note->type |= NOTE_FLAG_HIT;
	
			this->health -= 2000;

			if (this->character->spec & CHAR_SPEC_MISSANIM)
				this->character->set_anim(this->character, note_anims[type & 0x3][2]);
			else
				this->character->set_anim(this->character, note_anims[type & 0x3][0]);
			this->arrow_hitan[type & 0x3] = -1;
			
			return;
		}
	}
	
	//Missed a note
	this->arrow_hitan[type & 0x3] = -1;
	
	if (!stage.prefs.ghost)
	{
		if (stage.prefs.sfxmiss) 
			Audio_PlaySound(Sounds[RandomRange(4,6)], 0xBB8); //Randomly plays a miss sound
		
		if (this->character->spec & CHAR_SPEC_MISSANIM)
			this->character->set_anim(this->character, note_anims[type & 0x3][2]);

		else
			this->character->set_anim(this->character, note_anims[type & 0x3][0]);
		Stage_MissNote(this);
		
		this->health -= 400;
		this->score -= 1;
		this->refresh_score = true;
	}
}

static void Stage_SustainCheck(PlayerState *this, u8 type)
{
	//Perform note check
	for (Note *note = stage.cur_note;; note++)
	{
		//Check if note can be hit
		fixed_t note_fp = (fixed_t)note->pos << FIXED_SHIFT;
		if (note_fp - stage.early_sus_safe > stage.note_scroll)
			break;
		if (note_fp + stage.late_sus_safe < stage.note_scroll)
			continue;
		if ((note->type & NOTE_FLAG_HIT) || (note->type & (NOTE_FLAG_OPPONENT | 0x3)) != type || !(note->type & NOTE_FLAG_SUSTAIN))
			continue;
		
		//Hit the note
		note->type |= NOTE_FLAG_HIT;
		
		this->character->set_anim(this->character, note_anims[type & 0x3][(note->type & NOTE_FLAG_ALT_ANIM) != 0]);
		
		Stage_StartVocal();
		this->health += 230;
		this->arrow_hitan[type & 0x3] = stage.step_time;
			
	}
}

static void CheckNewScore()
{
	if (stage.mode == StageMode_Normal && !stage.prefs.botplay && !stage.prefs.practice && timer.timermin == 0 && timer.timer <= 5)
	{
		if (stage.player_state[0].score >= stage.prefs.savescore[stage.stage_id][stage.stage_diff])
			stage.prefs.savescore[stage.stage_id][stage.stage_diff] = stage.player_state[0].score;			
	}
}

static void Stage_ProcessPlayer(PlayerState *this, Pad *pad, boolean playing)
{
	//Handle player note presses
	if (stage.prefs.botplay == 0) {
		if (playing)
		{
			u8 i = ((this->character == stage.opponent) || (this->character == stage.opponent2)) ? NOTE_FLAG_OPPONENT : 0;
			
			this->pad_held = this->character->pad_held = pad->held;
			this->pad_press = pad->press;
			
			if (this->pad_held & INPUT_LEFT)
				Stage_SustainCheck(this, 0 | i);
			if (this->pad_held & INPUT_DOWN)
				Stage_SustainCheck(this, 1 | i);
			if (this->pad_held & INPUT_UP)
				Stage_SustainCheck(this, 2 | i);
			if (this->pad_held & INPUT_RIGHT)
				Stage_SustainCheck(this, 3 | i);
			
			if (this->pad_press & INPUT_LEFT)
				Stage_NoteCheck(this, 0 | i);
			if (this->pad_press & INPUT_DOWN)
				Stage_NoteCheck(this, 1 | i);
			if (this->pad_press & INPUT_UP)
				Stage_NoteCheck(this, 2 | i);
			if (this->pad_press & INPUT_RIGHT)
				Stage_NoteCheck(this, 3 | i);
		}
		else
		{
			this->pad_held = this->character->pad_held = 0;
			this->pad_press = 0;
		}
	}
	
	if (stage.prefs.botplay == 1) {
		//Do perfect note checks
		if (playing)
		{
			u8 i = ((this->character == stage.opponent) || (this->character == stage.opponent2)) ? NOTE_FLAG_OPPONENT : 0;
			
			u8 hit[4] = {0, 0, 0, 0};
			for (Note *note = stage.cur_note;; note++)
			{
				//Check if note can be hit
				fixed_t note_fp = (fixed_t)note->pos << FIXED_SHIFT;
				if (note_fp - stage.early_safe - FIXED_DEC(12,1) > stage.note_scroll)
					break;
				if (note_fp + stage.late_safe < stage.note_scroll)
					continue;
				if ((note->type & NOTE_FLAG_MINE) || (note->type & NOTE_FLAG_OPPONENT) != i)
					continue;
				
				//Handle note hit
				if (!(note->type & NOTE_FLAG_SUSTAIN))
				{
					if (note->type & NOTE_FLAG_HIT)
						continue;
					if (stage.note_scroll >= note_fp)
						hit[note->type & 0x3] |= 1;
					else if (!(hit[note->type & 0x3] & 8))
						hit[note->type & 0x3] |= 2;
				}
				else if (!(hit[note->type & 0x3] & 2))
				{
					if (stage.note_scroll <= note_fp)
						hit[note->type & 0x3] |= 4;
					hit[note->type & 0x3] |= 8;
				}
			}
			
			//Handle input
			this->pad_held = 0;
			this->pad_press = 0;
			
			for (u8 j = 0; j < 4; j++)
			{
				if (hit[j] & 5)
				{
					this->pad_held |= note_key[j];
					Stage_SustainCheck(this, j | i);
				}
				if (hit[j] & 1)
				{
					this->pad_press |= note_key[j];
					Stage_NoteCheck(this, j | i);
				}
			}
			
			this->character->pad_held = this->pad_held;
		}
		else
		{
			this->pad_held = this->character->pad_held = 0;
			this->pad_press = 0;
		}
	}
}

//Stage drawing functions
void Stage_DrawTexCol(Gfx_Tex *tex, const RECT *src, const RECT_FIXED *dst, fixed_t zoom, u8 cr, u8 cg, u8 cb)
{
	fixed_t xz = dst->x;
	fixed_t yz = dst->y;
	fixed_t wz = dst->w;
	fixed_t hz = dst->h;
	
	//Don't draw if HUD and is disabled
	if (tex == &stage.tex_hud0 || tex == &stage.tex_hud1)
	{
		#ifdef STAGE_NOHUD
			return;
		#endif
	}
	
	fixed_t l = (screen.SCREEN_WIDTH2  << FIXED_SHIFT) + FIXED_MUL(xz, zoom);// + FIXED_DEC(1,2);
	fixed_t t = (screen.SCREEN_HEIGHT2 << FIXED_SHIFT) + FIXED_MUL(yz, zoom);// + FIXED_DEC(1,2);
	fixed_t r = l + FIXED_MUL(wz, zoom);
	fixed_t b = t + FIXED_MUL(hz, zoom);
	
	l >>= FIXED_SHIFT;
	t >>= FIXED_SHIFT;
	r >>= FIXED_SHIFT;
	b >>= FIXED_SHIFT;
	
	RECT sdst = {
		l,
		t,
		r - l,
		b - t,
	};
	Gfx_DrawTexCol(tex, src, &sdst, cr, cg, cb);
}

void Stage_DrawTex(Gfx_Tex *tex, const RECT *src, const RECT_FIXED *dst, fixed_t zoom)
{
	Stage_DrawTexCol(tex, src, dst, zoom, 0x80, 0x80, 0x80);
}

void Stage_DrawTexRotate(Gfx_Tex *tex, const RECT *src, const RECT_FIXED *dst, fixed_t zoom, u8 angle)
{	
	s16 sin = MUtil_Sin(angle);
	s16 cos = MUtil_Cos(angle);
	int pw = dst->w / 2000;
    int ph = dst->h / 2000;

	//Get rotated points
	POINT p0 = {-pw, -ph};
	MUtil_RotatePoint(&p0, sin, cos);
	
	POINT p1 = { pw, -ph};
	MUtil_RotatePoint(&p1, sin, cos);
	
	POINT p2 = {-pw,  ph};
	MUtil_RotatePoint(&p2, sin, cos);
	
	POINT p3 = { pw,  ph};
	MUtil_RotatePoint(&p3, sin, cos);
	
	POINT_FIXED d0 = {
		dst->x + ((fixed_t)p0.x << FIXED_SHIFT),
		dst->y + ((fixed_t)p0.y << FIXED_SHIFT)
	};
	POINT_FIXED d1 = {
		dst->x + ((fixed_t)p1.x << FIXED_SHIFT),
		dst->y + ((fixed_t)p1.y << FIXED_SHIFT)
	};
	POINT_FIXED d2 = {
        dst->x + ((fixed_t)p2.x << FIXED_SHIFT),
		dst->y + ((fixed_t)p2.y << FIXED_SHIFT)
	};
	POINT_FIXED d3 = {
        dst->x + ((fixed_t)p3.x << FIXED_SHIFT),
		dst->y + ((fixed_t)p3.y << FIXED_SHIFT)
	};
	
    Stage_DrawTexArb(tex, src, &d0, &d1, &d2, &d3, zoom);
}

void Stage_BlendTexRotate(Gfx_Tex *tex, const RECT *src, const RECT_FIXED *dst, fixed_t zoom, u8 angle, u8 mode)
{	
	s16 sin = MUtil_Sin(angle);
	s16 cos = MUtil_Cos(angle);
	int pw = dst->w / 2000;
    int ph = dst->h / 2000;

	//Get rotated points
	POINT p0 = {-pw, -ph};
	MUtil_RotatePoint(&p0, sin, cos);
	
	POINT p1 = { pw, -ph};
	MUtil_RotatePoint(&p1, sin, cos);
	
	POINT p2 = {-pw,  ph};
	MUtil_RotatePoint(&p2, sin, cos);
	
	POINT p3 = { pw,  ph};
	MUtil_RotatePoint(&p3, sin, cos);
	
	POINT_FIXED d0 = {
		dst->x + ((fixed_t)p0.x << FIXED_SHIFT),
		dst->y + ((fixed_t)p0.y << FIXED_SHIFT)
	};
	POINT_FIXED d1 = {
		dst->x + ((fixed_t)p1.x << FIXED_SHIFT),
		dst->y + ((fixed_t)p1.y << FIXED_SHIFT)
	};
	POINT_FIXED d2 = {
        dst->x + ((fixed_t)p2.x << FIXED_SHIFT),
		dst->y + ((fixed_t)p2.y << FIXED_SHIFT)
	};
	POINT_FIXED d3 = {
        dst->x + ((fixed_t)p3.x << FIXED_SHIFT),
		dst->y + ((fixed_t)p3.y << FIXED_SHIFT)
	};
	
    Stage_BlendTexArb(tex, src, &d0, &d1, &d2, &d3, zoom, mode);
}

void Stage_BlendTex(Gfx_Tex *tex, const RECT *src, const RECT_FIXED *dst, fixed_t zoom, u8 mode)
{
	fixed_t xz = dst->x;
	fixed_t yz = dst->y;
	fixed_t wz = dst->w;
	fixed_t hz = dst->h;
	
	//Don't draw if HUD and is disabled
	if (tex == &stage.tex_hud0 || tex == &stage.tex_hud1)
	{
		#ifdef STAGE_NOHUD
			return;
		#endif
	}
	
	fixed_t l = (screen.SCREEN_WIDTH2  << FIXED_SHIFT) + FIXED_MUL(xz, zoom);// + FIXED_DEC(1,2);
	fixed_t t = (screen.SCREEN_HEIGHT2 << FIXED_SHIFT) + FIXED_MUL(yz, zoom);// + FIXED_DEC(1,2);
	fixed_t r = l + FIXED_MUL(wz, zoom);
	fixed_t b = t + FIXED_MUL(hz, zoom);
	
	l >>= FIXED_SHIFT;
	t >>= FIXED_SHIFT;
	r >>= FIXED_SHIFT;
	b >>= FIXED_SHIFT;
	
	RECT sdst = {
		l,
		t,
		r - l,
		b - t,
	};
	Gfx_BlendTex(tex, src, &sdst, mode);
}

void Stage_BlendTexV2(Gfx_Tex *tex, const RECT *src, const RECT_FIXED *dst, fixed_t zoom, u8 mode, u8 opacity)
{
	fixed_t xz = dst->x;
	fixed_t yz = dst->y;
	fixed_t wz = dst->w;
	fixed_t hz = dst->h;
	
	//Don't draw if HUD and is disabled
	if (tex == &stage.tex_hud0 || tex == &stage.tex_hud1)
	{
		#ifdef STAGE_NOHUD
			return;
		#endif
	}
	
	fixed_t l = (screen.SCREEN_WIDTH2  << FIXED_SHIFT) + FIXED_MUL(xz, zoom);// + FIXED_DEC(1,2);
	fixed_t t = (screen.SCREEN_HEIGHT2 << FIXED_SHIFT) + FIXED_MUL(yz, zoom);// + FIXED_DEC(1,2);
	fixed_t r = l + FIXED_MUL(wz, zoom);
	fixed_t b = t + FIXED_MUL(hz, zoom);
	
	l >>= FIXED_SHIFT;
	t >>= FIXED_SHIFT;
	r >>= FIXED_SHIFT;
	b >>= FIXED_SHIFT;
	
	RECT sdst = {
		l,
		t,
		r - l,
		b - t,
	};
	Gfx_BlendTexV2(tex, src, &sdst, mode, opacity);
}

void Stage_DrawTexArb(Gfx_Tex *tex, const RECT *src, const POINT_FIXED *p0, const POINT_FIXED *p1, const POINT_FIXED *p2, const POINT_FIXED *p3, fixed_t zoom)
{
	//Don't draw if HUD and HUD is disabled
	#ifdef STAGE_NOHUD
		if (tex == &stage.tex_hud0 || tex == &stage.tex_hud)
			return;
	#endif
	
	//Get screen-space points
	POINT s0 = {screen.SCREEN_WIDTH2 + (FIXED_MUL(p0->x, zoom) >> FIXED_SHIFT), screen.SCREEN_HEIGHT2 + (FIXED_MUL(p0->y, zoom) >> FIXED_SHIFT)};
	POINT s1 = {screen.SCREEN_WIDTH2 + (FIXED_MUL(p1->x, zoom) >> FIXED_SHIFT), screen.SCREEN_HEIGHT2 + (FIXED_MUL(p1->y, zoom) >> FIXED_SHIFT)};
	POINT s2 = {screen.SCREEN_WIDTH2 + (FIXED_MUL(p2->x, zoom) >> FIXED_SHIFT), screen.SCREEN_HEIGHT2 + (FIXED_MUL(p2->y, zoom) >> FIXED_SHIFT)};
	POINT s3 = {screen.SCREEN_WIDTH2 + (FIXED_MUL(p3->x, zoom) >> FIXED_SHIFT), screen.SCREEN_HEIGHT2 + (FIXED_MUL(p3->y, zoom) >> FIXED_SHIFT)};
	
	Gfx_DrawTexArb(tex, src, &s0, &s1, &s2, &s3);
}

void Stage_BlendTexArb(Gfx_Tex *tex, const RECT *src, const POINT_FIXED *p0, const POINT_FIXED *p1, const POINT_FIXED *p2, const POINT_FIXED *p3, fixed_t zoom, u8 mode)
{
	//Don't draw if HUD and HUD is disabled
	#ifdef STAGE_NOHUD
		if (tex == &stage.tex_hud0 || tex == &stage.tex_hud1)
			return;
	#endif
	
	//Get screen-space points
	POINT s0 = {screen.SCREEN_WIDTH2 + (FIXED_MUL(p0->x, zoom) >> FIXED_SHIFT), screen.SCREEN_HEIGHT2 + (FIXED_MUL(p0->y, zoom) >> FIXED_SHIFT)};
	POINT s1 = {screen.SCREEN_WIDTH2 + (FIXED_MUL(p1->x, zoom) >> FIXED_SHIFT), screen.SCREEN_HEIGHT2 + (FIXED_MUL(p1->y, zoom) >> FIXED_SHIFT)};
	POINT s2 = {screen.SCREEN_WIDTH2 + (FIXED_MUL(p2->x, zoom) >> FIXED_SHIFT), screen.SCREEN_HEIGHT2 + (FIXED_MUL(p2->y, zoom) >> FIXED_SHIFT)};
	POINT s3 = {screen.SCREEN_WIDTH2 + (FIXED_MUL(p3->x, zoom) >> FIXED_SHIFT), screen.SCREEN_HEIGHT2 + (FIXED_MUL(p3->y, zoom) >> FIXED_SHIFT)};
	
	Gfx_BlendTexArb(tex, src, &s0, &s1, &s2, &s3, mode);
}

//Stage HUD functions
static void Stage_DrawHealth(s16 health, u8 i, s8 ox)
{
//Check if we should use 'dying' frame
	s8 dying;
	if (ox < 0)
	{
		dying = (health >= 18000) * 46;
	}
    else
	{
		dying = (health <= 2000) * 46;
	}

	if (i <= 5)
	{
		//Get src and dst
		fixed_t hx = (128 << FIXED_SHIFT) * (10000 - health) / 10000;
		RECT src = {
			(i % 1) * 114 + dying,
			16 + (i / 1) * 46,
			46,
			46,
		};
		RECT_FIXED dst = {
			hx + ox * FIXED_DEC(15,1) - FIXED_DEC(21,1),
			FIXED_DEC(screen.SCREEN_HEIGHT2 - 32 + 4 - 23, 1),
			src.w << FIXED_SHIFT,
			src.h << FIXED_SHIFT
			};
		if (stage.prefs.downscroll)
			dst.y = -dst.y - dst.h;

		dst.y += stage.noteshakey;
		dst.x += stage.noteshakex;

		//Draw health icon
		if (stage.mode == StageMode_Swap)
		{
			dst.w = -dst.w;
			dst.x += FIXED_DEC(46,1);
		}
		else
		{
			dst.w = dst.w;
			dst.x = dst.x;
		}
		
		if (show)
			Stage_DrawTex(&stage.tex_hud1, &src, &dst, FIXED_MUL(stage.bump, stage.sbump));
	}
	else
	{
		//Get src and dst
		fixed_t hx = (128 << FIXED_SHIFT) * (10000 - health) / 10000;
		RECT src = {
			((i % 1) * 114 + dying) + 94,
			16 + ((i - 6) / 1) * 46,
			46,
			46,
		};
		RECT_FIXED dst = {
			hx + ox * FIXED_DEC(15,1) - FIXED_DEC(21,1),
			FIXED_DEC(screen.SCREEN_HEIGHT2 - 32 + 4 - 23, 1),
			src.w << FIXED_SHIFT,
			src.h << FIXED_SHIFT
			};
		if (stage.prefs.downscroll)
			dst.y = -dst.y - dst.h;

		dst.y += stage.noteshakey;
		dst.x += stage.noteshakex;

		//Draw health icon
		if (stage.mode == StageMode_Swap)
		{
			dst.w = -dst.w;
			dst.x += FIXED_DEC(46,1);
		}
		else
		{
			dst.w = dst.w;
			dst.x = dst.x;
		}
			
		if (show)
			Stage_DrawTex(&stage.tex_hud1, &src, &dst, FIXED_MUL(stage.bump, stage.sbump));
	}
}

static void Stage_DrawHealthBar(s16 x, s32 color)
{	
	//colors for health bar
	u8 red = (color >> 16) & 0xFF;
	u8 blue = (color >> 8) & 0xFF;
	u8 green = (color) & 0xFF;
	//Get src and dst
	RECT src = {
		0,
	    0,
		x,
		8
	};
		RECT_FIXED dst = {
			FIXED_DEC(-128,1), 
			(screen.SCREEN_HEIGHT2 - 32) << FIXED_SHIFT, 
			FIXED_DEC(src.w,1), 
			FIXED_DEC(8,1)
		};

	if (stage.prefs.downscroll)
		dst.y = -dst.y - dst.h;
	
	if (show)
		Stage_DrawTexCol(&stage.tex_hud1, &src, &dst, stage.bump, red >> 1, blue >> 1, green >> 1);
}

static void Stage_Player2(void)
{
	//check which mode you choose
	static char* checkoption;

	//change mode to single(only player2 sing)
	if (strcmp(stage.player2sing, "single") == 0 && checkoption != stage.player2sing)
	{
		if (stage.mode == StageMode_Swap)
		{
			stage.player_state[1].character->pad_held = 0;
			stage.player_state[1].character = stage.player2;
		}
		else
		{
			stage.player_state[0].character->pad_held = 0;
		 	stage.player_state[0].character = stage.player2;
		}
	}

	//change mode to none (player2 don't sing)
    else if (strcmp(stage.player2sing, "none") == 0 && checkoption != stage.player2sing)
	{
		if (stage.mode == StageMode_Swap)
		{
			stage.player_state[1].character->pad_held = 0;
		 	stage.player_state[1].character = stage.player;
		}
		else
		{
			stage.player_state[0].character->pad_held = 0;
			stage.player_state[0].character = stage.player;
		}
	}

	if (checkoption != stage.player2sing)
		checkoption = stage.player2sing;
}

static void Stage_Player3(void)
{
	//check which mode you choose
	static char* checkoption;

	//change mode to single(only player3 sing)
	if (strcmp(stage.player3sing, "single") == 0 && checkoption != stage.player3sing)
	{
		if (stage.mode == StageMode_Swap)
		{
			stage.player_state[1].character->pad_held = 0;
			stage.player_state[1].character = stage.player3;
		}
		else
		{
			stage.player_state[0].character->pad_held = 0;
		 	stage.player_state[0].character = stage.player3;
		}
	}

	//change mode to none (player3 don't sing)
    else if (strcmp(stage.player3sing, "none") == 0 && checkoption != stage.player3sing)
	{
		if (stage.mode == StageMode_Swap)
		{
			stage.player_state[1].character->pad_held = 0;
		 	stage.player_state[1].character = stage.player;
		}
		else
		{
			stage.player_state[0].character->pad_held = 0;
			stage.player_state[0].character = stage.player;
		}
	}

	if (checkoption != stage.player3sing)
		checkoption = stage.player3sing;
}

static void Stage_Player4(void)
{
	//check which mode you choose
	static char* checkoption;

	//change mode to single(only player4 sing)
	if (strcmp(stage.player4sing, "single") == 0 && checkoption != stage.player4sing)
	{
		if (stage.mode == StageMode_Swap)
		{
			stage.player_state[1].character->pad_held = 0;
			stage.player_state[1].character = stage.player4;
		}
		else
		{
			stage.player_state[0].character->pad_held = 0;
		 	stage.player_state[0].character = stage.player4;
		}
	}

	//change mode to none (player4 don't sing)
    else if (strcmp(stage.player4sing, "none") == 0 && checkoption != stage.player4sing)
	{
		if (stage.mode == StageMode_Swap)
		{
			stage.player_state[1].character->pad_held = 0;
		 	stage.player_state[1].character = stage.player;
		}
		else
		{
			stage.player_state[0].character->pad_held = 0;
			stage.player_state[0].character = stage.player;
		}
	}

	if (checkoption != stage.player4sing)
		checkoption = stage.player4sing;
}

static void Stage_Player5(void)
{
	//check which mode you choose
	static char* checkoption;

	//change mode to single(only player5 sing)
	if (strcmp(stage.player5sing, "single") == 0 && checkoption != stage.player5sing)
	{
		if (stage.mode == StageMode_Swap)
		{
			stage.player_state[1].character->pad_held = 0;
			stage.player_state[1].character = stage.player5;
		}
		else
		{
			stage.player_state[0].character->pad_held = 0;
		 	stage.player_state[0].character = stage.player5;
		}
	}

	//change mode to none (player5 don't sing)
    else if (strcmp(stage.player5sing, "none") == 0 && checkoption != stage.player5sing)
	{
		if (stage.mode == StageMode_Swap)
		{
			stage.player_state[1].character->pad_held = 0;
		 	stage.player_state[1].character = stage.player;
		}
		else
		{
			stage.player_state[0].character->pad_held = 0;
			stage.player_state[0].character = stage.player;
		}
	}

	if (checkoption != stage.player5sing)
		checkoption = stage.player5sing;
}

static void Stage_Opponent2(void)
{
	//check which mode you choose
	static char* checkoption;

	//change mode to single(only opponent2 sing)
	if (strcmp(stage.oppo2sing, "single") == 0 && checkoption != stage.oppo2sing)
	{
		if (stage.mode == StageMode_Swap)
		{
			stage.player_state[0].character->pad_held = 0;
			stage.player_state[0].character = stage.opponent2;
		}
		else
		{
			stage.player_state[1].character->pad_held = 0;
			stage.player_state[1].character = stage.opponent2;
		}
	}

	//change mode to none (opponent2 don't sing)
    else if (strcmp(stage.oppo2sing, "none") == 0 && checkoption != stage.oppo2sing)
	{
		if (stage.mode == StageMode_Swap)
		{
			stage.player_state[0].character->pad_held = 0;
		 	stage.player_state[0].character = stage.opponent;
		}
		else
		{
			stage.player_state[1].character->pad_held = 0;
		 	stage.player_state[1].character = stage.opponent;
		}
	}

	if (checkoption != stage.oppo2sing)
		checkoption = stage.oppo2sing;
}

static void Stage_Opponent3(void)
{
	//check which mode you choose
	static char* checkoption;

	//change mode to single(only opponent3 sing)
	if (strcmp(stage.oppo3sing, "single") == 0 && checkoption != stage.oppo3sing)
	{
		if (stage.mode == StageMode_Swap)
		{
			stage.player_state[0].character->pad_held = 0;
			stage.player_state[0].character = stage.opponent3;
		}
		else
		{
			stage.player_state[1].character->pad_held = 0;
			stage.player_state[1].character = stage.opponent3;
		}
	}

	//change mode to none (opponent3 don't sing)
    else if (strcmp(stage.oppo3sing, "none") == 0 && checkoption != stage.oppo3sing)
	{
		if (stage.mode == StageMode_Swap)
		{
			stage.player_state[0].character->pad_held = 0;
		 	stage.player_state[0].character = stage.opponent;
		}
		else
		{
			stage.player_state[1].character->pad_held = 0;
		 	stage.player_state[1].character = stage.opponent;
		}
	}

	if (checkoption != stage.oppo3sing)
		checkoption = stage.oppo3sing;
}

static void Stage_DrawStrum(u8 i, RECT *note_src, RECT_FIXED *note_dst)
{
	(void)note_dst;
	
	PlayerState *this = &stage.player_state[((i ^ stage.note_swap) & NOTE_FLAG_OPPONENT) != 0];
	i &= 0x3;
	
	if (this->arrow_hitan[i] > 0)
	{
		//Play hit animation
		u8 frame = ((this->arrow_hitan[i] << 1) / stage.step_time) & 1;
		note_src->x = (i + 1) << 5;
		note_src->y = 64 - (frame << 5);
		
		this->arrow_hitan[i] -= timer_dt;
		if (this->arrow_hitan[i] <= 0)
		{
			if (this->pad_held & note_key[i])
				this->arrow_hitan[i] = 1;
			else
				this->arrow_hitan[i] = 0;
		}
	}
	else if (this->arrow_hitan[i] < 0)
	{
		//Play depress animation
		note_src->x = (i + 1) << 5;
		note_src->y = 96;
		if (!(this->pad_held & note_key[i]))
			this->arrow_hitan[i] = 0;
	}
	else
	{
		note_src->x = 0;
		note_src->y = i << 5;
	}
}

static void Stage_DrawNotes(void)
{
	//Check if opponent should draw as bot
	u8 bot = (stage.mode >= StageMode_2P) ? 0 : NOTE_FLAG_OPPONENT;
	
	//Initialize scroll state
	SectionScroll scroll;
	scroll.start = stage.time_base;
	
	Section *scroll_section = stage.section_base;
	Stage_GetSectionScroll(&scroll, scroll_section);
	
	//Push scroll back until cur_note is properly contained
	while (scroll.start_step > stage.cur_note->pos)
	{
		//Look for previous section
		Section *prev_section = Stage_GetPrevSection(scroll_section);
		if (prev_section == NULL)
			break;
		
		//Push scroll back
		scroll_section = prev_section;
		Stage_GetSectionScroll(&scroll, scroll_section);
		scroll.start -= scroll.length;
	}
	
	//Draw notes
	for (Note *note = stage.cur_note; note->pos != 0xFFFF; note++)
	{
		//Update scroll
		while (note->pos >= scroll_section->end)
		{
			//Push scroll forward
			scroll.start += scroll.length;
			Stage_GetSectionScroll(&scroll, ++scroll_section);
		}
		
		//Get note information
		u8 i = ((note->type ^ stage.note_swap) & NOTE_FLAG_OPPONENT) != 0;
		PlayerState *this = &stage.player_state[i];
		
		fixed_t note_fp = (fixed_t)note->pos << FIXED_SHIFT;
		fixed_t time = (scroll.start - stage.song_time) + (scroll.length * (note->pos - scroll.start_step) / scroll.length_step);
		fixed_t y = note_y[(note->type & 0x7)] + FIXED_MUL(stage.speed, time * 150);
		
		//Check if went above screen
		if (y < FIXED_DEC(-16 - screen.SCREEN_HEIGHT2, 1))
		{
			//Wait for note to exit late time
			if (note_fp + stage.late_safe >= stage.note_scroll)
				continue;
			
			//Miss note if player's note
			if (!((note->type ^ stage.note_swap) & (bot | NOTE_FLAG_HIT | NOTE_FLAG_MINE)))
			{
				if (stage.mode < StageMode_Net1 || i == ((stage.mode == StageMode_Net1) ? 0 : 1))
				{
					//Missed note
					Stage_CutVocal();
					Stage_MissNote(this);
					this->health -= 475;
					
				}
			}
			
			//Update current note
			stage.cur_note++;
		}
		else
		{
			//Don't draw if below screen
			RECT note_src;
			RECT_FIXED note_dst;
			if (y > (FIXED_DEC(screen.SCREEN_HEIGHT,2) + scroll.size) || note->pos == 0xFFFF)
				break;
			
			//Draw note
			if (note->type & NOTE_FLAG_SUSTAIN)
			{
				//Check for sustain clipping
				fixed_t clip;
				y -= scroll.size;
				if (((note->type ^ stage.note_swap) & (bot | NOTE_FLAG_HIT)) || ((this->pad_held & note_key[note->type & 0x3]) && (note_fp + stage.late_sus_safe >= stage.note_scroll)))
				{
					clip = note_y[(note->type & 0x7)] - y;
					if (clip < 0)
						clip = 0;
				}
				else
				{
					clip = 0;
				}
				
				//Draw sustain
				if (note->type & NOTE_FLAG_SUSTAIN_END)
				{
					if (clip < (24 << FIXED_SHIFT))
					{
						note_src.x = 160;
						note_src.y = ((note->type & 0x3) << 5) + 4 + (clip >> FIXED_SHIFT);
						note_src.w = 32;
						note_src.h = 28 - (clip >> FIXED_SHIFT);
						
						note_dst.x = stage.noteshakex + note_x[(note->type & 0x7)] - FIXED_DEC(16,1);
						note_dst.y = stage.noteshakey + y + clip;
						note_dst.w = note_src.w << FIXED_SHIFT;
						note_dst.h = (note_src.h << FIXED_SHIFT);
						
						if (stage.prefs.downscroll)
						{
							note_dst.y = -note_dst.y;
							note_dst.h = -note_dst.h;
						}
						
						//draw for opponent
						if (show)
						{
							if (stage.prefs.middlescroll && note->type & NOTE_FLAG_OPPONENT)
								Stage_BlendTex(&stage.tex_hud0, &note_src, &note_dst, stage.bump, 1);
							else
								Stage_DrawTex(&stage.tex_hud0, &note_src, &note_dst, stage.bump);
						}
					}
				}
				else
				{
					//Get note height
					fixed_t next_time = (scroll.start - stage.song_time) + (scroll.length * (note->pos + 12 - scroll.start_step) / scroll.length_step);
					fixed_t next_y = note_y[(note->type & 0x7)] + FIXED_MUL(stage.speed, next_time * 150) - scroll.size;
					fixed_t next_size = next_y - y;
					
					if (clip < next_size)
					{
						note_src.x = 160;
						note_src.y = ((note->type & 0x3) << 5);
						note_src.w = 32;
						note_src.h = 16;
						
						note_dst.x = stage.noteshakex + note_x[(note->type & 0x7)] - FIXED_DEC(16,1);
						note_dst.y = stage.noteshakey + y + clip;
						note_dst.w = note_src.w << FIXED_SHIFT;
						note_dst.h = (next_y - y) - clip;
						
						if (stage.prefs.downscroll)
							note_dst.y = -note_dst.y - note_dst.h;

						//draw for opponent
						if (show)
						{
							if (stage.prefs.middlescroll && note->type & NOTE_FLAG_OPPONENT)
								Stage_BlendTex(&stage.tex_hud0, &note_src, &note_dst, stage.bump, 1);
							else
								Stage_DrawTex(&stage.tex_hud0, &note_src, &note_dst, stage.bump);
						}
					}
				}
			}
			else if (note->type & NOTE_FLAG_MINE)
			{
				//Don't draw if already hit
				if (note->type & NOTE_FLAG_HIT)
					continue;
				
				//Draw note body
				note_src.x = 192 + ((note->type & 0x1) << 5);
				note_src.y = (note->type & 0x2) << 4;
				note_src.w = 32;
				note_src.h = 32;
				
				note_dst.x = stage.noteshakex + note_x[(note->type & 0x7)] - FIXED_DEC(16,1);
				note_dst.y = stage.noteshakey + y - FIXED_DEC(16,1);
				note_dst.w = note_src.w << FIXED_SHIFT;
				note_dst.h = note_src.h << FIXED_SHIFT;
				
				if (stage.prefs.downscroll)
					note_dst.y = -note_dst.y - note_dst.h;

				//draw for opponent
				if (show)
				{
					if (stage.prefs.middlescroll && note->type & NOTE_FLAG_OPPONENT)
						Stage_BlendTex(&stage.tex_hud0, &note_src, &note_dst, stage.bump, 1);
					else
						Stage_DrawTex(&stage.tex_hud0, &note_src, &note_dst, stage.bump);
				}

			}
			else
			{
				//Don't draw if already hit
				if (note->type & NOTE_FLAG_HIT)
					continue;
				
				//Draw note
				note_src.x = 32 + ((note->type & 0x3) << 5);
				note_src.y = 0;
				note_src.w = 32;
				note_src.h = 32;
				
				note_dst.x = stage.noteshakex + note_x[(note->type & 0x7)] - FIXED_DEC(16,1);
				note_dst.y = stage.noteshakey + y - FIXED_DEC(16,1);
				note_dst.w = note_src.w << FIXED_SHIFT;
				note_dst.h = note_src.h << FIXED_SHIFT;
				
				if (stage.prefs.downscroll)
					note_dst.y = -note_dst.y - note_dst.h;
			
				//draw for opponent
				if (show)
				{
					if (stage.prefs.middlescroll && note->type & NOTE_FLAG_OPPONENT)
						Stage_BlendTex(&stage.tex_hud0, &note_src, &note_dst, stage.bump, 1);
					else
						Stage_DrawTex(&stage.tex_hud0, &note_src, &note_dst, stage.bump);
				}
			}
		}
	}
}

static void Stage_ChangeZoom(int zoom, const char *mode)
{
	if (strcmp(mode, "change") == 0)
	{
		zooming = true;
		stage.camera.tz = zoom;
	}
	else if (strcmp(mode, "default") == 0)
		zooming = false;

}

int soundcooldown;
int drawshit;

static void Stage_CountDown(void)
{
	switch(stage.song_step)
	{
		case -20:
			drawshit = 4;
			if (soundcooldown == 0)
				Audio_PlaySound(Sounds[0], 0x3fff);
			soundcooldown ++;
			break;
		case -16:
			soundcooldown = 0;
			break;
		case -15:
			drawshit = 3;
			if (soundcooldown == 0)
				Audio_PlaySound(Sounds[1], 0x3fff);
			soundcooldown ++;
			break;
		case -11:
			soundcooldown = 0;
			break;
		case -10:
			drawshit = 2;
			if (soundcooldown == 0)
				Audio_PlaySound(Sounds[2], 0x3fff);
			soundcooldown ++;
			break;
		case -6:
			soundcooldown = 0;
			break;
		case -5:
			drawshit = 1;
			if (soundcooldown == 0)
				Audio_PlaySound(Sounds[3], 0x3fff);
			soundcooldown ++;
			break;
	}

	RECT three_src = {  0, 77, 71, 75};	
	RECT_FIXED three_dst = {FIXED_DEC(-36,1), FIXED_DEC(-38,1), FIXED_DEC(75,1), FIXED_DEC(71,1)};
	
	RECT two_src = { 72, 73, 83, 79};	
	RECT_FIXED two_dst = {FIXED_DEC(-41,1), FIXED_DEC(-40,1), FIXED_DEC(79,1), FIXED_DEC(83,1)};	

	RECT one_src = {  0,  0, 72, 72};	
	RECT_FIXED one_dst = {FIXED_DEC(-36,1), FIXED_DEC(-36,1), FIXED_DEC(72,1), FIXED_DEC(72,1)};	

	RECT go_src = {  73,  5,134, 67};	
	RECT_FIXED go_dst = {FIXED_DEC(-67,1), FIXED_DEC(-34,1), FIXED_DEC(134,1), FIXED_DEC(67,1)};	

	if (drawshit == 4 && stage.song_step >= -20 && stage.song_step <= -17)
		Stage_DrawTex(&stage.tex_countdown, &three_src, &three_dst, stage.bump);
	else if (drawshit == 4 && stage.song_step >= -17 && stage.song_step <= -16)
		Stage_BlendTex(&stage.tex_countdown, &three_src, &three_dst, stage.bump,1);

	if (drawshit == 3 && stage.song_step >= -15 && stage.song_step <= -12)
		Stage_DrawTex(&stage.tex_countdown, &two_src, &two_dst, stage.bump);
	else if (drawshit == 3 && stage.song_step >= -12 && stage.song_step <= -11)
		Stage_BlendTex(&stage.tex_countdown, &two_src, &two_dst, stage.bump,1);

	if (drawshit == 2 && stage.song_step >= -10 && stage.song_step <= -7)
		Stage_DrawTex(&stage.tex_countdown, &one_src, &one_dst, stage.bump);
	else if (drawshit == 2 && stage.song_step >= -7 && stage.song_step <= -6)
		Stage_BlendTex(&stage.tex_countdown, &one_src, &one_dst, stage.bump,1);

	if (drawshit == 1 && stage.song_step >= -5 && stage.song_step <= -2)
		Stage_DrawTex(&stage.tex_countdown, &go_src, &go_dst, stage.bump);
	else if (drawshit == 1 && stage.song_step >= -2 && stage.song_step <= -1)
		Stage_BlendTex(&stage.tex_countdown, &go_src, &go_dst, stage.bump,1);
}

//Stage loads
static void Stage_LoadPlayer(void)
{
	//Load player character
	Character_Free(stage.player);
	stage.player = stage.stage_def->pchar.new(stage.stage_def->pchar.x, stage.stage_def->pchar.y);
}

static void Stage_LoadPlayer2(void)
{
	//Load player character
	Character_Free(stage.player2);
	if (stage.stage_def->pchar2.new != NULL) {
		stage.player2 = stage.stage_def->pchar2.new(stage.stage_def->pchar2.x, stage.stage_def->pchar2.y);
	}
	else
		stage.player2 = NULL;
}

static void Stage_LoadPlayer3(void)
{
	//Load player character
	Character_Free(stage.player3);
	if (stage.stage_def->pchar3.new != NULL) {
		stage.player3 = stage.stage_def->pchar3.new(stage.stage_def->pchar3.x, stage.stage_def->pchar3.y);
	}
	else
		stage.player3 = NULL;
}

static void Stage_LoadPlayer4(void)
{
	//Load player character
	Character_Free(stage.player4);
	if (stage.stage_def->pchar4.new != NULL) {
		stage.player4 = stage.stage_def->pchar4.new(stage.stage_def->pchar4.x, stage.stage_def->pchar4.y);
	}
	else
		stage.player4 = NULL;
}

static void Stage_LoadPlayer5(void)
{
	//Load player character
	Character_Free(stage.player5);
	if (stage.stage_def->pchar5.new != NULL) {
		stage.player5 = stage.stage_def->pchar5.new(stage.stage_def->pchar5.x, stage.stage_def->pchar5.y);
	}
	else
		stage.player5 = NULL;
}

static void Stage_LoadOpponent(void)
{
	//Load opponent character
	Character_Free(stage.opponent);
	stage.opponent = stage.stage_def->ochar.new(stage.stage_def->ochar.x, stage.stage_def->ochar.y);
}

static void Stage_LoadOpponent2(void)
{
	//Load opponent character
	Character_Free(stage.opponent2);
	if (stage.stage_def->ochar2.new != NULL) {
		stage.opponent2 = stage.stage_def->ochar2.new(stage.stage_def->ochar2.x, stage.stage_def->ochar2.y);
	}
	else
		stage.opponent2 = NULL;
}

static void Stage_LoadOpponent3(void)
{
	//Load opponent character
	Character_Free(stage.opponent3);
	if (stage.stage_def->ochar3.new != NULL) {
		stage.opponent3 = stage.stage_def->ochar3.new(stage.stage_def->ochar3.x, stage.stage_def->ochar3.y);
	}
	else
		stage.opponent3 = NULL;
}

static void Stage_LoadGirlfriend(void)
{
	//Load girlfriend character
	Character_Free(stage.gf);
	if (stage.stage_def->gchar.new != NULL)
		stage.gf = stage.stage_def->gchar.new(stage.stage_def->gchar.x, stage.stage_def->gchar.y);
	else
		stage.gf = NULL;
}

static void Stage_LoadStage(void)
{
	//Load back
	if (stage.back != NULL)
		stage.back->free(stage.back);
	stage.back = stage.stage_def->back();
}

static void Stage_LoadChart(void)
{
	//Load stage data
	char chart_path[64];
	
	//Use standard path convention
	sprintf(chart_path, "\\WEEK%d\\%d.%d%c.CHT;1", stage.stage_def->week, stage.stage_def->week, stage.stage_def->week_song, "ENH"[stage.stage_diff]);
	
	
	if (stage.chart_data != NULL)
		Mem_Free(stage.chart_data);
	stage.chart_data = IO_Read(chart_path);
	u8 *chart_byte = (u8*)stage.chart_data;

		//Directly use section and notes pointers
		stage.sections = (Section*)(chart_byte + 6);
		stage.notes = (Note*)(chart_byte + ((u16*)stage.chart_data)[2]);
		
		for (Note *note = stage.notes; note->pos != 0xFFFF; note++)
			stage.num_notes++;
	
	//Count max scores
	stage.player_state[0].max_score = 0;
	stage.player_state[1].max_score = 0;
	for (Note *note = stage.notes; note->pos != 0xFFFF; note++)
	{
		if (note->type & (NOTE_FLAG_SUSTAIN | NOTE_FLAG_MINE))
			continue;
		if (note->type & NOTE_FLAG_OPPONENT)
			stage.player_state[1].max_score += 35;
		else
			stage.player_state[0].max_score += 35;
	}
	if (stage.mode >= StageMode_2P && stage.player_state[1].max_score > stage.player_state[0].max_score)
		stage.max_score = stage.player_state[1].max_score;
	else
		stage.max_score = stage.player_state[0].max_score;
	
	stage.cur_section = stage.sections;
	stage.cur_note = stage.notes;
	
	stage.speed = *((fixed_t*)stage.chart_data);
	
	stage.step_crochet = 0;
	stage.time_base = 0;
	stage.step_base = 0;
	stage.section_base = stage.cur_section;
	Stage_ChangeBPM(stage.cur_section->flag & SECTION_FLAG_BPM_MASK, 0);
}

static void Stage_LoadSFX(void)
{
	//Load SFX
	CdlFILE file;

	//intro sound
	for (u8 i = 0; i < 4;i++)
	{
		char text[0x80];
		sprintf(text, "\\SOUNDS\\INTRO%d.VAG;1", i);
	  	IO_FindFile(&file, text);
	    u32 *data = IO_ReadFile(&file);
	    Sounds[i] = Audio_LoadVAGData(data, file.size);
	    Mem_Free(data);
	}

	//miss sound
	if (stage.prefs.sfxmiss)
	{
		for (u8 i = 0; i < 3;i++)
		{
			char text[0x80];
			sprintf(text, "\\SOUNDS\\MISS%d.VAG;1", i + 1);
		  	IO_FindFile(&file, text);
		    u32 *data = IO_ReadFile(&file);
		    Sounds[i + 4] = Audio_LoadVAGData(data, file.size);
		    Mem_Free(data);
		}
    }
	
	//bite sound
	if ((stage.stage_id == StageId_4_3) || (stage.stage_id == StageId_5_3))
	{
		char text[0x80];
		sprintf(text, "\\SOUNDS\\BITE.VAG;1");
		IO_FindFile(&file, text);
		u32 *data = IO_ReadFile(&file);
		Sounds[9] = Audio_LoadVAGData(data, file.size);
		Mem_Free(data);
	}
	
	//glitch sound
	if (stage.stage_id == StageId_5_3)
	{
		char text[0x80];
		sprintf(text, "\\SOUNDS\\GLITCH.VAG;1");
		IO_FindFile(&file, text);
		u32 *data = IO_ReadFile(&file);
		Sounds[8] = Audio_LoadVAGData(data, file.size);
		Mem_Free(data);
	}
	
	//criminal sound
	if (stage.stage_id == StageId_6_2)
	{
		char text[0x80];
		sprintf(text, "\\SOUNDS\\CRIMINAL.VAG;1");
		IO_FindFile(&file, text);
		u32 *data = IO_ReadFile(&file);
		Sounds[9] = Audio_LoadVAGData(data, file.size);
		Mem_Free(data);
	}

}

static void Stage_LoadMusic(void)
{
	//Offset sing ends
	stage.player->sing_end -= stage.note_scroll;
	if (stage.player2 != NULL)
		stage.player2->sing_end -= stage.note_scroll;
	if (stage.player3 != NULL)
		stage.player3->sing_end -= stage.note_scroll;
	if (stage.player4 != NULL)
		stage.player4->sing_end -= stage.note_scroll;
	if (stage.player5 != NULL)
		stage.player5->sing_end -= stage.note_scroll;
	stage.opponent->sing_end -= stage.note_scroll;
	if (stage.opponent2 != NULL)
		stage.opponent2->sing_end -= stage.note_scroll;
	if (stage.opponent3 != NULL)
		stage.opponent3->sing_end -= stage.note_scroll;
	if (stage.gf != NULL)
		stage.gf->sing_end -= stage.note_scroll;
	
	//Find music file and begin seeking to it
	Audio_SeekXA_Track(stage.stage_def->music_track);
	
	//Initialize music state
		//added more steps and disable intro
	if (stage.stage_id == StageId_4_3)
	{
		stage.intro = true;
		stage.note_scroll = FIXED_DEC(-8 * 6 * 12,1);
	}
	else if (stage.stage_id == StageId_5_3)
	{
		stage.intro = false;
		stage.note_scroll = FIXED_DEC(-71 * 1 * 12,1);
	}
	else if (stage.stage_id == StageId_6_2)
	{
		stage.intro = false;
		stage.note_scroll = FIXED_DEC(-61 * 2 * 12,1);
	}
	else
	{
		stage.intro = true;
		stage.note_scroll = FIXED_DEC(-5 * 6 * 12,1);
	}
	stage.song_time = FIXED_DIV(stage.note_scroll, stage.step_crochet);
	stage.interp_time = 0;
	stage.interp_ms = 0;
	stage.interp_speed = 0;
	
	//Offset sing ends again
	stage.player->sing_end += stage.note_scroll;
	if (stage.player2 != NULL)
		stage.player2->sing_end += stage.note_scroll;
	if (stage.player3 != NULL)
		stage.player3->sing_end += stage.note_scroll;
	if (stage.player4 != NULL)
		stage.player4->sing_end += stage.note_scroll;
	if (stage.player5 != NULL)
		stage.player5->sing_end += stage.note_scroll;
	stage.opponent->sing_end += stage.note_scroll;
	if (stage.opponent2 != NULL)
		stage.opponent2->sing_end += stage.note_scroll;
	if (stage.opponent3 != NULL)
		stage.opponent3->sing_end += stage.note_scroll;
	if (stage.gf != NULL)
		stage.gf->sing_end += stage.note_scroll;
}

static void Stage_LoadState(void)
{
	//Initialize stage state
	stage.flag = STAGE_FLAG_VOCAL_ACTIVE;
	
	stage.gf_speed = 1 << 2;
	
	stage.state = StageState_Play;
	
	if (stage.mode == StageMode_Swap)
	{
		stage.player_state[0].character = stage.opponent;
		stage.player_state[1].character = stage.player;
	}
	else
	{
		stage.player_state[0].character = stage.player;
		stage.player_state[1].character = stage.opponent;
	}

	for (int i = 0; i < 2; i++)
	{
		memset(stage.player_state[i].arrow_hitan, 0, sizeof(stage.player_state[i].arrow_hitan));
		
		stage.player_state[i].health = 10000;
		stage.player_state[i].combo = 0;
		stage.oppo2sing = "none";
		stage.oppo3sing = "none";
		stage.player2sing = "none";
		stage.player3sing = "none";
		stage.player4sing = "none";
		stage.player5sing = "none";
		soundcooldown = 0;
		drawshit = 0;
		if (!stage.prefs.debug)
			stage.freecam = 0;
		zooming = false;
		stage.player_state[i].refresh_miss = false;
		stage.player_state[i].miss = 0;
		stage.player_state[i].refresh_accuracy = false;
		stage.player_state[i].accuracy = 0;
		stage.player_state[i].max_accuracy = 0;
		stage.player_state[i].min_accuracy = 0;
		stage.player_state[i].refresh_score = false;
		stage.player_state[i].score = 0;
		stage.song_beat = 0;
		timer.secondtimer = 0;
		timer.timer = Audio_GetLength(stage.stage_def->music_track) - 1;
		timer.timermin = 0;
		strcpy(stage.player_state[i].accuracy_text, "Accuracy: ?");
		strcpy(stage.player_state[i].miss_text, "Misses: 0");
		strcpy(stage.player_state[i].score_text, "Score: 0");
		
		stage.player_state[i].pad_held = stage.player_state[i].pad_press = 0;
	}
	
	//BF
	note_y[0] = FIXED_DEC(32 - screen.SCREEN_HEIGHT2 + 5, 1);
	note_y[1] = FIXED_DEC(32 - screen.SCREEN_HEIGHT2 + 5, 1);//+34
	note_y[2] = FIXED_DEC(32 - screen.SCREEN_HEIGHT2 + 5, 1);
	note_y[3] = FIXED_DEC(32 - screen.SCREEN_HEIGHT2 + 5, 1);
	//Opponent
	note_y[4] = FIXED_DEC(32 - screen.SCREEN_HEIGHT2 + 5, 1);
	note_y[5] = FIXED_DEC(32 - screen.SCREEN_HEIGHT2 + 5, 1);//+34
	note_y[6] = FIXED_DEC(32 - screen.SCREEN_HEIGHT2 + 5, 1);
	note_y[7] = FIXED_DEC(32 - screen.SCREEN_HEIGHT2 + 5, 1);

	//middle note x
	if(stage.prefs.middlescroll || stage.stage_id == StageId_4_3)
	{
		//bf
		note_x[0] = FIXED_DEC(26 - 78,1) + FIXED_DEC(screen.SCREEN_WIDEADD,4);
		note_x[1] = FIXED_DEC(60 - 78,1) + FIXED_DEC(screen.SCREEN_WIDEADD,4); //+34
		note_x[2] = FIXED_DEC(94 - 78,1) + FIXED_DEC(screen.SCREEN_WIDEADD,4);
		note_x[3] = FIXED_DEC(128 - 78,1) + FIXED_DEC(screen.SCREEN_WIDEADD,4);
		//opponent
	    note_x[4] = FIXED_DEC(-50 - 78,1) - FIXED_DEC(screen.SCREEN_WIDEADD,4);
		note_x[5] = FIXED_DEC(-16 - 78,1) - FIXED_DEC(screen.SCREEN_WIDEADD,4); //+34
		note_x[6] = FIXED_DEC(170 - 78,1) - FIXED_DEC(screen.SCREEN_WIDEADD,4);
		note_x[7] = FIXED_DEC(204 - 78,1) - FIXED_DEC(screen.SCREEN_WIDEADD,4);
	}
	else
	{
		//bf
		note_x[0] = FIXED_DEC(26,1) + FIXED_DEC(screen.SCREEN_WIDEADD,4);
		note_x[1] = FIXED_DEC(60,1) + FIXED_DEC(screen.SCREEN_WIDEADD,4); //+34
		note_x[2] = FIXED_DEC(94,1) + FIXED_DEC(screen.SCREEN_WIDEADD,4);
		note_x[3] = FIXED_DEC(128,1) + FIXED_DEC(screen.SCREEN_WIDEADD,4);
		//opponent
		note_x[4] = FIXED_DEC(-128,1) - FIXED_DEC(screen.SCREEN_WIDEADD,4);
		note_x[5] = FIXED_DEC(-94,1) - FIXED_DEC(screen.SCREEN_WIDEADD,4); //+34
		note_x[6] = FIXED_DEC(-60,1) - FIXED_DEC(screen.SCREEN_WIDEADD,4);
		note_x[7] = FIXED_DEC(-26,1) - FIXED_DEC(screen.SCREEN_WIDEADD,4);
	}

	if (stage.stage_id == StageId_4_3 || stage.stage_id == StageId_6_2)
	{

		note_x[4] = FIXED_DEC(1000,1);
		note_x[5] = FIXED_DEC(1000,1);
		note_x[6] = FIXED_DEC(1000,1);
		note_x[7] = FIXED_DEC(1000,1);
	}

	if (stage.stage_id == StageId_6_2)
	{
		note_x[0] = FIXED_DEC(-128,1) + FIXED_DEC(screen.SCREEN_WIDEADD,4);
		note_x[1] = FIXED_DEC(-86,1) + FIXED_DEC(screen.SCREEN_WIDEADD,4); //+34
		note_x[2] = FIXED_DEC(86,1) + FIXED_DEC(screen.SCREEN_WIDEADD,4);
		note_x[3] = FIXED_DEC(128,1) + FIXED_DEC(screen.SCREEN_WIDEADD,4);
	}

	ObjectList_Free(&stage.objlist_splash);
	ObjectList_Free(&stage.objlist_fg);
	ObjectList_Free(&stage.objlist_bg);
}

//Stage functions
char iconpath[30];

void Stage_Load(StageId id, StageDiff difficulty, boolean story)
{
	//Get stage definition
	stage.stage_def = &stage_defs[stage.stage_id = id];
	stage.stage_diff = difficulty;
	stage.story = story;
	
	//Load HUD textures
	if (id != StageId_2_2 && id != StageId_3_2 && id != StageId_3_3 && id != StageId_4_4 && id != StageId_6_2)
		Gfx_LoadTex(&stage.tex_hud0, IO_Read("\\STAGE\\HUD0WEEB.TIM;1"), GFX_LOADTEX_FREE);
	else
		Gfx_LoadTex(&stage.tex_hud0, IO_Read("\\STAGE\\HUD0.TIM;1"), GFX_LOADTEX_FREE);
	
	sprintf(iconpath, "\\STAGE\\HUD1-%d.TIM;1", stage.stage_def->week);
	Gfx_LoadTex(&stage.tex_hud1, IO_Read(iconpath), GFX_LOADTEX_FREE);
	Gfx_LoadTex(&stage.tex_countdown, IO_Read("\\STAGE\\COUNT.TIM;1"), GFX_LOADTEX_FREE);
	if (id == StageId_1_3)
		Gfx_LoadTex(&stage.tex_flashb, IO_Read("\\STAGE\\FLASHB.TIM;1"), GFX_LOADTEX_FREE);
	if (id == StageId_1_3 || id == StageId_1_4 || id == StageId_4_1 || id == StageId_4_3 || id == StageId_5_3)
		Gfx_LoadTex(&stage.tex_scanline, IO_Read("\\STAGE\\SCANLINE.TIM;1"), GFX_LOADTEX_FREE);
	if (id == StageId_5_2 || id == StageId_6_1 || id == StageId_6_3)
		Gfx_LoadTex(&stage.tex_border, IO_Read("\\STAGE\\BORDER.TIM;1"), GFX_LOADTEX_FREE);
	if (id == StageId_5_3)
		Gfx_LoadTex(&stage.tex_alert, IO_Read("\\BLACK2\\ALERT.TIM;1"), GFX_LOADTEX_FREE);

	//Load stage background
	Stage_LoadStage();

	//Load SFX
	Stage_LoadSFX();

	//load fonts
	FontData_Load(&stage.font_cdr, Font_CDR);
	FontData_Load(&stage.font_bold, Font_Bold);

	//Load characters
	Stage_LoadPlayer();
	Stage_LoadPlayer2();
	Stage_LoadPlayer3();
	Stage_LoadPlayer4();
	Stage_LoadPlayer5();
	Stage_LoadOpponent();
	Stage_LoadOpponent2();
	Stage_LoadOpponent3();	
	Stage_LoadGirlfriend();
	
	//Load stage chart
	Stage_LoadChart();
	
	//Initialize stage state
	stage.story = story;
	
	Stage_LoadState();
	
	//Initialize camera
	if (stage.cur_section->flag & SECTION_FLAG_OPPFOCUS || stage.stage_id == StageId_4_2 || stage.stage_id == StageId_4_3 || stage.stage_id == StageId_5_3 || stage.stage_id == StageId_6_2)
		Stage_FocusCharacter(stage.opponent, FIXED_UNIT);
	else
		Stage_FocusCharacter(stage.player, FIXED_UNIT);
	stage.camera.x = stage.camera.tx;
	stage.camera.y = stage.camera.ty;
	stage.camera.zoom = stage.camera.tz;
	
	stage.bump = FIXED_UNIT;
	stage.sbump = FIXED_UNIT;
	
	//Initialize stage according to mode
	stage.note_swap = (stage.mode == StageMode_Swap && (!(stage.prefs.middlescroll))) ? 4 : 0;
	
	//Load music
	stage.note_scroll = 0;
	Stage_LoadMusic();
	
	//Test offset
	stage.offset = 0;
}

void Stage_Unload(void)
{
	//Disable net mode to not break the game
	if (stage.mode >= StageMode_Net1)
		stage.mode = StageMode_Normal;
	
	//Unload stage background
	if (stage.back != NULL)
		stage.back->free(stage.back);
	stage.back = NULL;
	
	//Unload stage data
	Mem_Free(stage.chart_data);
	stage.chart_data = NULL;
	
	//Free objects
	ObjectList_Free(&stage.objlist_splash);
	ObjectList_Free(&stage.objlist_fg);
	ObjectList_Free(&stage.objlist_bg);
	
	//Free characters
	Character_Free(stage.player);
	stage.player = NULL;
	Character_Free(stage.player2);
	stage.player2 = NULL;
	Character_Free(stage.player3);
	stage.player3 = NULL;
	Character_Free(stage.player4);
	stage.player4 = NULL;
	Character_Free(stage.player5);
	stage.player5 = NULL;
	Character_Free(stage.opponent);
	stage.opponent = NULL;
	Character_Free(stage.opponent2);
	stage.opponent2 = NULL;
	Character_Free(stage.opponent3);
	stage.opponent3 = NULL;
	Character_Free(stage.gf);
	stage.gf = NULL;
}

static boolean Stage_NextLoad(void)
{
	CheckNewScore();
	writeSaveFile();

	u16 load = stage.stage_def->next_load;
	if (load == 0)
	{
		//Do stage transition if full reload
		stage.trans = StageTrans_NextSong;
		Trans_Start();
		return false;
	}
	else
	{
		//Get stage definition
		stage.stage_def = &stage_defs[stage.stage_id = stage.stage_def->next_stage];
		
		//Load stage background
		if (load & STAGE_LOAD_STAGE)
			Stage_LoadStage();
		
		//Load characters
		if (load & STAGE_LOAD_PLAYER)
		{
			Stage_LoadPlayer();
		}
		else
		{
			stage.player->x = stage.stage_def->pchar.x;
			stage.player->y = stage.stage_def->pchar.y;
		}
		if (load & STAGE_LOAD_PLAYER2)
		{
			Stage_LoadPlayer2();
		}
		else if (stage.player2 != NULL)
		{
			stage.player2->x = stage.stage_def->pchar2.x;
			stage.player2->y = stage.stage_def->pchar2.y;
		}
		if (load & STAGE_LOAD_PLAYER3)
		{
			Stage_LoadPlayer3();
		}
		else if (stage.player3 != NULL)
		{
			stage.player3->x = stage.stage_def->pchar3.x;
			stage.player3->y = stage.stage_def->pchar3.y;
		}
		if (load & STAGE_LOAD_PLAYER4)
		{
			Stage_LoadPlayer4();
		}
		else if (stage.player4 != NULL)
		{
			stage.player4->x = stage.stage_def->pchar4.x;
			stage.player4->y = stage.stage_def->pchar4.y;
		}
		if (load & STAGE_LOAD_PLAYER5)
		{
			Stage_LoadPlayer5();
		}
		else if (stage.player5 != NULL)
		{
			stage.player5->x = stage.stage_def->pchar5.x;
			stage.player5->y = stage.stage_def->pchar5.y;
		}

		if (load & STAGE_LOAD_OPPONENT)
		{
			Stage_LoadOpponent();
		}
		else if (stage.opponent2 != NULL)
		{
			stage.opponent->x = stage.stage_def->ochar.x;
			stage.opponent->y = stage.stage_def->ochar.y;
		}
		if (load & STAGE_LOAD_OPPONENT2)
		{
			Stage_LoadOpponent2();
		}
		else if (stage.opponent2 != NULL)
		{
			stage.opponent2->x = stage.stage_def->ochar2.x;
			stage.opponent2->y = stage.stage_def->ochar2.y;
		}
		if (load & STAGE_LOAD_OPPONENT3)
		{
			Stage_LoadOpponent3();
		}
		else if (stage.opponent3 != NULL)
		{
			stage.opponent3->x = stage.stage_def->ochar3.x;
			stage.opponent3->y = stage.stage_def->ochar3.y;
		}
		if (load & STAGE_LOAD_GIRLFRIEND)
		{
			Stage_LoadGirlfriend();
		}
		else if (stage.gf != NULL)
		{
			stage.gf->x = stage.stage_def->gchar.x;
			stage.gf->y = stage.stage_def->gchar.y;
		}
		
		//Load stage chart
		Stage_LoadChart();
		
		//Initialize stage state
		Stage_LoadState();
		
		//Load music
		Stage_LoadMusic();
		
		//Reset timer
		Timer_Reset();
		return true;
	}
}

static int deadtimer;
static boolean inctimer;

void Stage_Tick(void)
{
	SeamLoad:;
	
	//Tick transition
	//Return to menu when start is pressed
	
	if (pad_state.press & (PAD_START | PAD_CROSS) && stage.state != StageState_Play)
	{
		if (deadtimer == 0)
		{
			inctimer = true;
			Audio_StopXA();
			Audio_PlaySound(Sounds[1], 0x3fff);
		}
	}
	else if (pad_state.press & PAD_CIRCLE && stage.state != StageState_Play)
	{
		stage.trans = StageTrans_Menu;
		Trans_Start();
	}

	if (inctimer)
		deadtimer ++;

	if (deadtimer == 200 && stage.state != StageState_Play)
	{
		stage.trans = StageTrans_Reload;
		Trans_Start();
	}

	if (Trans_Tick())
	{
		switch (stage.trans)
		{
			case StageTrans_Menu:
				CheckNewScore();
				writeSaveFile();
				//Load appropriate menu
				Stage_Unload();
				
				LoadScr_Start();
		
					if (stage.stage_id <= StageId_LastVanilla)
					{
						if (stage.story)
							Menu_Load(MenuPage_Main);
						else
							Menu_Load(MenuPage_Freeplay);
					}
					else
					{
						Menu_Load(MenuPage_Credits);
					}
				
				LoadScr_End();
				
				gameloop = GameLoop_Menu;
				return;
			case StageTrans_NextSong:
				//Load next song
				Stage_Unload();
				
				LoadScr_Start();
				Stage_Load(stage.stage_def->next_stage, stage.stage_diff, stage.story);
				LoadScr_End();
				break;
			case StageTrans_Reload:
				//Reload song
				Stage_Unload();
				
				LoadScr_Start();
				Stage_Load(stage.stage_id, stage.stage_diff, stage.story);
				LoadScr_End();
				stage.paused = false;
				break;
		}
	}
	
	switch (stage.state)
	{
		case StageState_Play:
		{ 
			  if (stage.prefs.debug)
					Debug_StageDebug();
				
				FntPrint("step is %d", stage.song_step);
				
				//Draw FlashB
				if ((stage.stage_id == StageId_1_3 && stage.song_step >= 256 && stage.song_step <= 271) || (stage.stage_id == StageId_1_3 && stage.song_step >= 2192 && stage.song_step <= 2239))
				{
					RECT flashb_src = {0, 0, 256, 256};
					RECT flashb_dst = {0, 0, (screen.SCREEN_WIDTH + 10), (screen.SCREEN_HEIGHT + 1)};
					Gfx_DrawTex(&stage.tex_flashb, &flashb_src, &flashb_dst);
				}
				
				//Draw white fade
				if ((stage.stage_id == StageId_1_3 && stage.song_step == 911) || (stage.stage_id == StageId_1_3 && stage.song_step == 1166) || (stage.stage_id == StageId_1_3 && stage.song_step == 1423) || (stage.stage_id == StageId_1_3 && stage.song_step == 1679))
				{
					fade = FIXED_DEC(255,1);
					fadespd = FIXED_DEC(175,1);
				}
				if (stage.stage_id == StageId_5_3 && stage.song_step == 543)
				{
					fade = FIXED_DEC(255,1);
					fadespd = FIXED_DEC(275,1);
				}
				if (stage.stage_id == StageId_5_3 && stage.song_step == 2472)
				{
					fade = FIXED_DEC(255,1);
					fadespd = FIXED_DEC(60,1);
				}
				if (fade > 0)
				{
					RECT flash = {0, 0, screen.SCREEN_WIDTH, screen.SCREEN_HEIGHT};
					u8 flash_col = fade >> FIXED_SHIFT;
					Gfx_BlendRect(&flash, flash_col, flash_col, flash_col, 1);
					fade -= FIXED_MUL(fadespd, timer_dt);
				}
				
				//Draw scanline
				if (stage.prefs.scanline == true)
				{
					if ((stage.stage_id == StageId_1_4) || (stage.stage_id == StageId_4_1) || (stage.stage_id == StageId_4_3) || (stage.stage_id == StageId_5_3))
					{
						RECT scanline_src = {0, 0, 256, 256};
						RECT scanline_dst = {0, 0, (screen.SCREEN_WIDTH + 10), (screen.SCREEN_HEIGHT + 1)};
						Gfx_DrawTex(&stage.tex_scanline, &scanline_src, &scanline_dst);
					}
					else if ((stage.stage_id == StageId_1_3 && stage.song_step >= 911 && stage.song_step <= 1167) || (stage.stage_id == StageId_1_3 && stage.song_step >= 1423 && stage.song_step <= 1680))
					{
						RECT scanline_src = {0, 0, 256, 256};
						RECT scanline_dst = {0, 0, (screen.SCREEN_WIDTH + 10), (screen.SCREEN_HEIGHT + 1)};
						Gfx_DrawTex(&stage.tex_scanline, &scanline_src, &scanline_dst);
					}
				}
				
				//Draw alert
				if (stage.stage_id == StageId_5_3)
				{
					RECT alert_src = {0, 0, 72, 36};
					RECT alert_dst = {0, 0, (screen.SCREEN_WIDTH), (screen.SCREEN_HEIGHT)};
					if ((stage.song_step >= 1316) && (stage.song_step <= 1824) && ((stage.song_beat % 2) == 1) || (stage.song_step >= 2476) && (stage.song_step <= 2735) && ((stage.song_beat % 2) == 1))
						Gfx_BlendTex(&stage.tex_alert, &alert_src, &alert_dst, 1);
				}
			
			if (stage.prefs.songtimer)
				if ((stage.stage_id != StageId_4_3) && (stage.stage_id != StageId_6_2))
					if (show)
						StageTimer_Draw();
					
			if ((stage.stage_id == StageId_4_3) && (stage.song_step == -37) && stage.flag & STAGE_FLAG_JUST_STEP)
				Audio_PlaySound(Sounds[9], 0x3fff);
			
			//if ((stage.stage_id == StageId_5_3) && (stage.song_step == -37) && stage.flag & STAGE_FLAG_JUST_STEP)
			//	Audio_PlaySound(Sounds[8], 0x3fff);
			
			//if ((stage.stage_id == StageId_5_3) && (stage.song_step == -37) && stage.flag & STAGE_FLAG_JUST_STEP)
			//	Audio_PlaySound(Sounds[9], 0x3fff);
			
			if ((stage.stage_id == StageId_6_2) && (stage.song_step == -122) && stage.flag & STAGE_FLAG_JUST_STEP)
				Audio_PlaySound(Sounds[9], 0x3fff);
			
			if (stage.stage_id == StageId_1_3)
			{
				if (stage.song_step >= 2193)
					show = false;
				else 
					show = true;
			}
			else if (stage.stage_id == StageId_3_2)
			{
				if ((stage.song_step >= 1032) && (stage.song_step <= 1056))
					show = false;
				else
					show = true;
			}
			else if (stage.stage_id == StageId_4_3)
			{
				if (stage.song_step <= -25)
					show = false;
				else
					show = true;
			}
			else if ((stage.stage_id == StageId_5_3) || (stage.stage_id == StageId_6_2))
			{
				if (stage.song_step <= 0)
					show = false;
				else
					show = true;
			}
			else
			{
				show = true;
			}
			
			//FntPrint("step %d, beat %d", stage.song_step, stage.song_beat);
			if (stage.intro)
				Stage_CountDown();

			Stage_Player2();
			Stage_Player3();
			Stage_Player4();
			Stage_Player5();
			Stage_Opponent2();
			Stage_Opponent3();

			if (stage.prefs.botplay)
			{
				//Draw botplay
				RECT bot_src = {174, 225, 67, 16};
				RECT_FIXED bot_dst = {FIXED_DEC(-33,1), FIXED_DEC(-60,1), FIXED_DEC(67,1), FIXED_DEC(16,1)};

				bot_dst.y += stage.noteshakey;
				bot_dst.x += stage.noteshakex;
				
				if (show)
					if (!stage.prefs.debug)
						Stage_DrawTex(&stage.tex_hud0, &bot_src, &bot_dst, stage.bump);
			}

			if (noteshake) 
			{
				stage.noteshakex = RandomRange(FIXED_DEC(-5,1),FIXED_DEC(5,1));
				stage.noteshakey = RandomRange(FIXED_DEC(-5,1),FIXED_DEC(5,1));
			}
			else
			{
				stage.noteshakex = 0;
				stage.noteshakey = 0;
			}

			//Clear per-frame flags
			stage.flag &= ~(STAGE_FLAG_JUST_STEP | STAGE_FLAG_SCORE_REFRESH);
			
			//Get song position
			boolean playing;
			fixed_t next_scroll;
			
			const fixed_t interp_int = FIXED_UNIT * 8 / 75;

			if (!stage.paused)
			{
				if (stage.note_scroll < 0)
				{
					//Play countdown sequence
					stage.song_time += timer_dt;
						
					//Update song
					if (stage.song_time >= 0)
					{
						//Song has started
						playing = true;

						Audio_PlayXA_Track(stage.stage_def->music_track, 0x40, stage.stage_def->music_channel, 0);

						//Update song time
						fixed_t audio_time = (fixed_t)Audio_TellXA_Milli() - stage.offset;
						if (audio_time < 0)
							audio_time = 0;
						stage.interp_ms = (audio_time << FIXED_SHIFT) / 1000;
						stage.interp_time = 0;
						stage.song_time = stage.interp_ms;
					}
					else
					{
						//Still scrolling
						playing = false;
					}
					
					//Update scroll
					next_scroll = FIXED_MUL(stage.song_time, stage.step_crochet);
				}
				else if (Audio_PlayingXA())
				{
					fixed_t audio_time_pof = (fixed_t)Audio_TellXA_Milli();
					fixed_t audio_time = (audio_time_pof > 0) ? (audio_time_pof - stage.offset) : 0;

					if (stage.prefs.expsync)
					{
						//Get playing song position
						if (audio_time_pof > 0)
						{
							stage.song_time += timer_dt;
							stage.interp_time += timer_dt;
						}

						if (stage.interp_time >= interp_int)
						{
							//Update interp state
							while (stage.interp_time >= interp_int)
								stage.interp_time -= interp_int;
							stage.interp_ms = (audio_time << FIXED_SHIFT) / 1000;
						}

						//Resync
						fixed_t next_time = stage.interp_ms + stage.interp_time;
						if (stage.song_time >= next_time + FIXED_DEC(25,1000) || stage.song_time <= next_time - FIXED_DEC(25,1000))
						{
							stage.song_time = next_time;
						}
						else
						{
							if (stage.song_time < next_time - FIXED_DEC(1,1000))
								stage.song_time += FIXED_DEC(1,1000);
							if (stage.song_time > next_time + FIXED_DEC(1,1000))
								stage.song_time -= FIXED_DEC(1,1000);
						}
					}
					else
					{
						//Old sync
						stage.interp_ms = (audio_time << FIXED_SHIFT) / 1000;
						stage.interp_time = 0;
						stage.song_time = stage.interp_ms;
					}

					playing = true;

					//Update scroll
					next_scroll = ((fixed_t)stage.step_base << FIXED_SHIFT) + FIXED_MUL(stage.song_time - stage.time_base, stage.step_crochet);
				}
				else
				{
					//Song has ended
					playing = false;
					stage.song_time += timer_dt;

					//Update scroll
					next_scroll = ((fixed_t)stage.step_base << FIXED_SHIFT) + FIXED_MUL(stage.song_time - stage.time_base, stage.step_crochet);
					
					//Transition to menu or next song
					if (stage.story && stage.stage_def->next_stage != stage.stage_id)
					{
						if (Stage_NextLoad())
							goto SeamLoad;
					}
					else if (stage.paused == false)
					{
						stage.trans = StageTrans_Menu;
						Trans_Start();
					}
				}	

				RecalcScroll:;
				//Update song scroll and step
				if (next_scroll > stage.note_scroll)
				{
					if (((stage.note_scroll / 12) & FIXED_UAND) != ((next_scroll / 12) & FIXED_UAND))
						stage.flag |= STAGE_FLAG_JUST_STEP;
					stage.note_scroll = next_scroll;
					stage.song_step = (stage.note_scroll >> FIXED_SHIFT);
					if (stage.note_scroll < 0)
						stage.song_step -= 11;
					stage.song_step /= 12;
				}

				//Update section
				if (stage.note_scroll >= 0)
				{
					//Check if current section has ended
					u16 end = stage.cur_section->end;
					if ((stage.note_scroll >> FIXED_SHIFT) >= end)
					{
						//Increment section pointer
						stage.cur_section++;

						//Update BPM
						u16 next_bpm = stage.cur_section->flag & SECTION_FLAG_BPM_MASK;
						Stage_ChangeBPM(next_bpm, end);
						stage.section_base = stage.cur_section;

						//Recalculate scroll based off new BPM
						next_scroll = ((fixed_t)stage.step_base << FIXED_SHIFT) + FIXED_MUL(stage.song_time - stage.time_base, stage.step_crochet);
						goto RecalcScroll;
					}
				}
			}


			if (stage.paused == false && pad_state.press & PAD_START)
				{
					stage.pause_scroll = -1;
					Audio_PauseXA();
					stage.paused = true;
					pad_state.press = 0;
				}

			if (stage.paused)
				{
					switch (stage.pause_state)
					{
						case 0:
							PausedState();
							break;
						case 1:
							OptionsState(&note_x);
							break;
					}
				}

			//Handle bump
			if ((stage.bump = FIXED_UNIT + FIXED_MUL(stage.bump - FIXED_UNIT, FIXED_DEC(95,100))) <= FIXED_DEC(1003,1000))
				stage.bump = FIXED_UNIT;
			stage.sbump = FIXED_UNIT + FIXED_MUL(stage.sbump - FIXED_UNIT, FIXED_DEC(60,100));
			
			if (playing && (stage.flag & STAGE_FLAG_JUST_STEP))
			{
				//Check if screen should bump
				boolean is_bump_step = (stage.song_step & 0xF) == 0;
				
				//Bump screen
				if (is_bump_step)
					stage.bump = FIXED_DEC(103,100);

				//Bump health every 4 steps
				if ((stage.song_step & 0x3) == 0)
					stage.sbump = FIXED_DEC(103,100);
			}
			
			//Scroll camera
			if ((stage.cur_section->flag & SECTION_FLAG_OPPFOCUS) || (stage.stage_id == StageId_4_2) || (stage.stage_id == StageId_4_3) || (stage.stage_id == StageId_5_3 || stage.stage_id == StageId_6_2))
				Stage_FocusCharacter(stage.opponent, FIXED_UNIT / 24);
			else
				Stage_FocusCharacter(stage.player, FIXED_UNIT / 24);
			Stage_ScrollCamera();
			
			//Draw Score
			for (int i = 0; i < ((stage.mode >= StageMode_2P) ? 2 : 1); i++)
			{
				PlayerState *this = &stage.player_state[i];
					
				if (this->refresh_score)
				{
					if (this->score != 0)
						sprintf(this->score_text, "Score: %d0", this->score * stage.max_score / this->max_score);
					else
						strcpy(this->score_text, "Score: 0");
					this->refresh_score = false;
				}
				
				if (show)
				{
					stage.font_cdr.draw(&stage.font_cdr,
						this->score_text,
						(stage.mode == StageMode_2P && i == 0) ? FIXED_DEC(10,1) : FIXED_DEC(-150,1), 
						(screen.SCREEN_HEIGHT2 - 22) << FIXED_SHIFT,
						FontAlign_Left
					);
				}
			}
				
			//Draw Combo Break
			for (int i = 0; i < ((stage.mode >= StageMode_2P) ? 2 : 1); i++)
			{
				PlayerState *this = &stage.player_state[i];

				if (this->refresh_miss)
				{
					if (this->miss != 0)
						sprintf(this->miss_text, "Misses: %d", this->miss);
					else
						strcpy(this->miss_text, "Misses: 0");
					this->refresh_miss = false;
				}

				if (show)
				{
					stage.font_cdr.draw(&stage.font_cdr,
						this->miss_text,
						(stage.mode == StageMode_2P && i == 0) ? FIXED_DEC(100,1) : FIXED_DEC(-60,1), 
						(screen.SCREEN_HEIGHT2 - 22) << FIXED_SHIFT,
						FontAlign_Left
					);
				}
			}
				
			//Draw Accuracy
			for (int i = 0; i < ((stage.mode >= StageMode_2P) ? 2 : 1); i++)
			{
				PlayerState *this = &stage.player_state[i];
			
				this->accuracy = (this->min_accuracy * 100) / (this->max_accuracy);

				//Rank
				if (this->accuracy == 100 && this->miss == 0)
					strcpy(this->rank, "[SFC]");
				else if (this->accuracy >= 80 && this->miss == 0)
					strcpy(this->rank, "[GFC]");
				else if (this->miss == 0)
					strcpy(this->rank, "[FC]");
				else
					strcpy(this->rank, "");
				
				if (this->refresh_accuracy)
				{
					if (this->accuracy != 0)
						sprintf(this->accuracy_text, "Accuracy: %d%% %s", this->accuracy, this->rank);
					else
						strcpy(this->accuracy_text, "Accuracy: ?");	
					this->refresh_accuracy = false;
				}
				//sorry for this shit lmao
				if (show)
				{
					stage.font_cdr.draw(&stage.font_cdr,
						this->accuracy_text,
						(stage.mode == StageMode_2P && i == 0) ? FIXED_DEC(50,1) : (stage.mode == StageMode_2P && i == 1) ? FIXED_DEC(-110,1) : FIXED_DEC(39,1), 
						(stage.mode == StageMode_2P) ? FIXED_DEC(85,1) : (screen.SCREEN_HEIGHT2 - 22) << FIXED_SHIFT,
						FontAlign_Left
					);
				}
			}
			
			switch (stage.mode)
			{
				case StageMode_Normal:
				case StageMode_Swap:
				{
					//Handle player 1 inputs
					Stage_ProcessPlayer(&stage.player_state[0], &pad_state, playing);
					
					//Handle opponent notes
					u8 opponent_anote = CharAnim_Idle;
					u8 opponent_snote = CharAnim_Idle;
						
						for (Note *note = stage.cur_note;; note++)
					{
						if (note->pos > (stage.note_scroll >> FIXED_SHIFT))
							break;
						
						//Opponent note hits
						if (playing && ((note->type ^ stage.note_swap) & NOTE_FLAG_OPPONENT) && !(note->type & NOTE_FLAG_HIT))
						{
							//Opponent hits note
							stage.player_state[1].arrow_hitan[note->type & 0x3] = stage.step_time;
							Stage_StartVocal();
							if (note->type & NOTE_FLAG_SUSTAIN)
								opponent_snote = note_anims[note->type & 0x3][(note->type & NOTE_FLAG_ALT_ANIM) != 0];
							else
								opponent_anote = note_anims[note->type & 0x3][(note->type & NOTE_FLAG_ALT_ANIM) != 0];
							note->type |= NOTE_FLAG_HIT;
						}
					}
					
					if (opponent_anote != CharAnim_Idle)
						stage.player_state[1].character->set_anim(stage.player_state[1].character, opponent_anote);
					else if (opponent_snote != CharAnim_Idle)
						stage.player_state[1].character->set_anim(stage.player_state[1].character, opponent_snote);
					break;
					break;
				}
				case StageMode_2P:
				{
					//Handle player 1 and 2 inputs
					Stage_ProcessPlayer(&stage.player_state[0], &pad_state, playing);
					Stage_ProcessPlayer(&stage.player_state[1], &pad_state_2, playing);
					break;
				}
			}

			if (!stage.prefs.debug)
			{
				if (stage.mode < StageMode_2P)
				{
					//Perform health checks
					if (stage.player_state[0].health <= 0 && stage.prefs.practice == 0)
					{
						//Player has died
						stage.player_state[0].health = 0;
							
						stage.state = StageState_Dead;
					}
					if (stage.player_state[0].health > 20000)
						stage.player_state[0].health = 20000;

					if (stage.player_state[0].health <= 0 && stage.prefs.practice)
						stage.player_state[0].health = 0;

					//Draw health heads
					Stage_DrawHealth(stage.player_state[0].health, stage.player_state[0].character->health_i,    1);
					Stage_DrawHealth(stage.player_state[0].health, stage.player_state[1].character->health_i, -1);
					
                    //Draw health bar
                    if (stage.mode == StageMode_Swap)
                    {
						Stage_DrawHealthBar(255 - (255 * stage.player_state[0].health / 20000), stage.player->health_bar);
						Stage_DrawHealthBar(255, stage.opponent->health_bar);
					}
					else
					{
						Stage_DrawHealthBar(255 - (255 * stage.player_state[0].health / 20000), stage.opponent->health_bar);
						Stage_DrawHealthBar(255, stage.player->health_bar);
					}
				}
			
				//Tick note splashes
				ObjectList_Tick(&stage.objlist_splash);
				
				//Draw stage notes
				Stage_DrawNotes();
				
				//Draw note HUD
				RECT note_src = {0, 0, 32, 32};
				RECT_FIXED note_dst = {0, 0 + stage.noteshakey, FIXED_DEC(32,1), FIXED_DEC(32,1)};
				
				for (u8 i = 0; i < 4; i++)
				{
					//BF
					note_dst.x = stage.noteshakex + note_x[i] - FIXED_DEC(16,1);
					note_dst.y = stage.noteshakey + note_y[i] - FIXED_DEC(16,1);
					if (stage.prefs.downscroll)
						note_dst.y = -note_dst.y - note_dst.h;
					
					Stage_DrawStrum(i, &note_src, &note_dst);

					if (show)
						Stage_DrawTex(&stage.tex_hud0, &note_src, &note_dst, stage.bump);
					
					//Opponent
					note_dst.x = stage.noteshakex + note_x[(i | 0x4)] - FIXED_DEC(16,1);
					note_dst.y = stage.noteshakey + note_y[(i | 0x4)] - FIXED_DEC(16,1);
					
					if (stage.prefs.downscroll)
						note_dst.y = -note_dst.y - note_dst.h;
					Stage_DrawStrum(i | 4, &note_src, &note_dst);

					if (show)
					{
						if ((stage.stage_id != StageId_4_3)|| (stage.stage_id != StageId_6_2))
						{
							if (stage.prefs.middlescroll)
								Stage_BlendTex(&stage.tex_hud0, &note_src, &note_dst, stage.bump, 1);
							else
								Stage_DrawTex(&stage.tex_hud0, &note_src, &note_dst, stage.bump);
						}
					}
				}
			}
			
			//Draw border
			if ((stage.stage_id == StageId_5_2) || (stage.stage_id == StageId_6_1) || (stage.stage_id == StageId_6_3))
			{
				RECT border_src = {0, 0, 100, 240};
				RECT border_dst = {0, 0, (screen.SCREEN_WIDTH + 10), (screen.SCREEN_HEIGHT)};
				Gfx_DrawTex(&stage.tex_border, &border_src, &border_dst);
			}
			
			//Draw stage foreground
			if (stage.back->draw_fg != NULL)
				stage.back->draw_fg(stage.back);
			
			//Tick foreground objects
			ObjectList_Tick(&stage.objlist_fg);
			
			//Tick characters
			if (stage.mode == StageMode_Swap)
			{
				stage.opponent->tick(stage.opponent);
				stage.player->tick(stage.player);
			}
			else
			{
				stage.player->tick(stage.player);
				stage.opponent->tick(stage.opponent);
			}
            if (stage.player2 != NULL)
				stage.player2->tick(stage.player2);
			if (stage.player3 != NULL)
				stage.player3->tick(stage.player3);
			if (stage.player4 != NULL)
				stage.player4->tick(stage.player4);
			if (stage.player5 != NULL)
				stage.player5->tick(stage.player5);

            if (stage.opponent2 != NULL)
				stage.opponent2->tick(stage.opponent2);
			if (stage.opponent3 != NULL)
				stage.opponent3->tick(stage.opponent3);
			
			//Draw stage middle
			if (stage.back->draw_md != NULL)
				stage.back->draw_md(stage.back);
			
			//Tick girlfriend
			if (stage.gf != NULL)
				stage.gf->tick(stage.gf);
			
			//Tick background objects
			ObjectList_Tick(&stage.objlist_bg);
			
			//Draw stage background
			if (stage.back->draw_bg != NULL)
				stage.back->draw_bg(stage.back);
			
			if (stage.song_step > 0)
			{
				stage.song_beat = stage.song_step / 4;
				
				StageTimer_Tick();
			}
			else
				StageTimer_Calculate();


			if (stage.stage_id == StageId_1_3)
			{
				if (stage.song_step == 270)
					stage.oppo2sing = "single";
				if (stage.song_step == 271)
					stage.player2sing = "single";
				if (stage.song_step == 911)
					stage.oppo2sing = "none";
				if (stage.song_step == 912)
					stage.oppo3sing = "single";
				if (stage.song_step == 1167)
					stage.oppo3sing = "none";
				if (stage.song_step == 1168)
					stage.oppo2sing = "single";
				if (stage.song_step == 1423)
					stage.oppo2sing = "none";
				if (stage.song_step == 1424)
					stage.oppo3sing = "single";
				if (stage.song_step == 1680)
					stage.oppo3sing = "none";
				if (stage.song_step == 1681)
					stage.player2sing = "none";
			}
			if (stage.stage_id == StageId_2_1)
			{
				if (stage.song_step == 1784)
					stage.player2sing = "single";
				if (stage.song_step == 1924)
					stage.player2sing = "none";
				if (stage.song_step == 2046)
					stage.player2sing = "single";
				if (stage.song_step == 2181)
					stage.player2sing = "none";
				if (stage.song_step == 2372)
					stage.player2sing = "single";
				if (stage.song_step == 2431)
					stage.player2sing = "none";
			}
			if (stage.stage_id == StageId_2_3)
			{
				if (stage.song_step == 824)
					stage.player2sing = "single";
				if (stage.song_step == 895)
					stage.player2sing = "none";
				if (stage.song_step == 1343)
					stage.oppo2sing = "single";
				if (stage.song_step == 1407)
					stage.oppo2sing = "none";
				if (stage.song_step == 2104)
					stage.player2sing = "single";
				if (stage.song_step == 2175)
					stage.player2sing = "none";
			}
			if (stage.stage_id == StageId_3_2)
			{
				if (stage.song_step == 496)
					stage.oppo2sing = "single";
				if (stage.song_step == 1037)
					stage.oppo2sing = "none";
				if (stage.song_step == 1038)
					stage.oppo3sing = "single";
				if (stage.song_step == 1472)
					stage.oppo3sing = "none";
			}
			break;
		}
	    case StageState_Dead: //Start BREAK animation and reading extra data from CD
		{
			//Stop music immediately
			Audio_StopXA();
			deadtimer = 0;
			inctimer = false;
			
			//Unload stage data
			Mem_Free(stage.chart_data);
			stage.chart_data = NULL;
			
			//Free background
			stage.back->free(stage.back);
			stage.back = NULL;
			
			//Free objects
			ObjectList_Free(&stage.objlist_fg);
			ObjectList_Free(&stage.objlist_bg);
			
			//Free opponent and girlfriend
			Character_Free(stage.player2);
			stage.player2 = NULL;
			Character_Free(stage.player3);
			stage.player3 = NULL;
			Character_Free(stage.player4);
			stage.player4 = NULL;
			Character_Free(stage.player5);
			stage.player5 = NULL;
			Character_Free(stage.opponent);
			stage.opponent = NULL;
            Character_Free(stage.opponent2);
			stage.opponent2 = NULL;
			Character_Free(stage.opponent3);
			stage.opponent3 = NULL;
			Character_Free(stage.gf);
			stage.gf = NULL;
			
			//Reset stage state
			stage.flag = 0;
			stage.bump = stage.sbump = FIXED_UNIT;
			
			//Change background colour to black
			Gfx_SetClear(0, 0, 0);
			
			stage.song_time = 0;
			
			if ((stage.stage_id == StageId_2_2) || (stage.stage_id == StageId_3_3) || (stage.stage_id == StageId_4_4))
				Gfx_LoadTex(&stage.tex_ded, IO_Read("\\CHAR\\DEAD.TIM;1"), GFX_LOADTEX_FREE);
			else if (stage.stage_id == StageId_2_1)
				Gfx_LoadTex(&stage.tex_ded, IO_Read("\\CHAR\\DEADOU.TIM;1"), GFX_LOADTEX_FREE);
			else if (stage.stage_id == StageId_3_1)
				Gfx_LoadTex(&stage.tex_ded, IO_Read("\\CHAR\\DEADF.TIM;1"), GFX_LOADTEX_FREE);
			else if (stage.stage_id == StageId_3_2)
				Gfx_LoadTex(&stage.tex_ded, IO_Read("\\CHAR\\DEADM.TIM;1"), GFX_LOADTEX_FREE);
			else if (stage.stage_id == StageId_4_1)
				Gfx_LoadTex(&stage.tex_ded, IO_Read("\\CHAR2\\DEADC.TIM;1"), GFX_LOADTEX_FREE);
			else if ((stage.stage_id == StageId_4_3) || (stage.stage_id == StageId_6_1))
				Gfx_LoadTex(&stage.tex_ded, IO_Read("\\CHAR2\\DEADCC.TIM;1"), GFX_LOADTEX_FREE);
			else if (stage.stage_id == StageId_5_2)
				Gfx_LoadTex(&stage.tex_ded, IO_Read("\\CHAR2\\DEADJ.TIM;1"), GFX_LOADTEX_FREE);
			else
				Gfx_LoadTex(&stage.tex_ded, IO_Read("\\CHAR\\DEADO.TIM;1"), GFX_LOADTEX_FREE);
		
			Audio_PlayXA_Track(XA_GameOver, 0x40, 1, true);	
			stage.state = StageState_DeadLoad;
		}
	//Fallthrough
		case StageState_DeadLoad:
		{
			if (stage.stage_id == StageId_3_1)
			{
				RECT src = {0, 0,255,255};
				RECT dst = { 0, 0, 380, 255};
				Gfx_DrawTex(&stage.tex_ded, &src, &dst);
			}
			else
			{
				RECT src = {0, 0,255,255};
				RECT dst = { 33,-8,255,255};
				Gfx_DrawTex(&stage.tex_ded, &src, &dst);
			}
			break;
		}
		default:
			break;
	}
}
