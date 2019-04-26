/*
Copyright (C) 2019 Parallel Realities

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "manholeCover.h"

static void tick(void);
static void touch(Entity *other);
static void die(void);

void initManholeCover(Entity *e)
{
	Collectable *m;
	
	m = malloc(sizeof(Collectable));
	memset(m, 0, sizeof(Collectable));
	
	e->typeName = "manholeCover";
	e->type = ET_ITEM;
	e->data = m;
	e->atlasImage = getAtlasImage("gfx/entities/manholeCover.png", 1);
	e->w = e->atlasImage->rect.w;
	e->h = e->atlasImage->rect.h;
	e->flags = EF_WEIGHTLESS+EF_NO_ENT_CLIP;
	e->tick = tick;
	e->touch = touch;
	e->die = die;
}

static void tick(void)
{
	Collectable *m;
	
	m = (Collectable*)self->data;
	
	m->bobValue += 0.1;
	
	self->y += sin(m->bobValue) * 0.25;
}

static void touch(Entity *other)
{
	Walter *w;
	
	if (self->health > 0 && other != NULL && (other->type == ET_PLAYER || other->type == ET_CLONE))
	{
		w = (Walter*)other->data;
		
		if (w->equipment == EQ_NONE)
		{
			self->health = 0;
			
			w->equipment = EQ_MANHOLE_COVER;
			
			playPositionalSound(SND_MANHOLE_COVER, CH_ITEM, self->x, self->y, stage.player->x, stage.player->y);
			
			game.stats[STAT_MANHOLE_COVERS]++;
		}
	}
}

static void die(void)
{
	addPowerupParticles(self->x + self->w / 2, self->y + self->h / 2);
}
