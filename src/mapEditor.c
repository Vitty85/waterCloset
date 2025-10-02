/*
Copyright (C) 2019,2022 Parallel Realities

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

#include "common.h"
#include "mapEditor.h"
#include "json/cJSON.h"
#include "world/quadtree.h"
#include "system/atlas.h"
#include "system/util.h"
#include "system/input.h"
#include "world/map.h"
#include "system/init.h"
#include "world/stage.h"
#include "system/text.h"
#include "system/io.h"
#include "world/entities.h"
#include "system/draw.h"
#include "world/entityFactory.h"

#include <SDL2/SDL_ttf.h>
#include <dirent.h>

#define WINDOW_W 640
#define WINDOW_H 480

enum
{
	MODE_TILE,
	MODE_ENT,
	MODE_PICK
};

App app;
Entity *player;
Entity *self;
Game game;
Stage stage;

static void capFrameRate(long *then, float *remainder);

static int tile;
static int cameraTimer;
static Entity **entities;
static int numEnts;
static int entIndex;
static Entity *entity;
static Entity *selectedEntity;
static int mode;

#ifdef _WIN32
static void saveMap(cJSON *root)
{
	int x, y;
	char *buff;
	size_t bufsize;
	char *ptr;
	int written;
	size_t remaining;

	bufsize = (MAP_WIDTH * MAP_HEIGHT * 12) + 1;
	buff = (char*)malloc(bufsize);
	if (!buff) {
		return;
	}

	ptr = buff;
	remaining = bufsize;

	for (y = 0 ; y < MAP_HEIGHT ; y++) {
		for (x = 0 ; x < MAP_WIDTH ; x++) {
			written = snprintf(ptr, remaining, "%d ", stage.map[x][y]);
			if (written < 0 || (size_t)written >= remaining) {
				// buffer esaurito → tronca
				buff[bufsize - 1] = '\0';
				goto done;
			}
			ptr += written;
			remaining -= written;
		}
	}

	done:
		cJSON_AddStringToObject(root, "map", buff);
		
	free(buff);
}
#else
static void saveMap(cJSON *root)
{
	int x, y;
	unsigned long l;
	FILE *fp;
	char *buff;

	fp = open_memstream(&buff, &l);

	for (y = 0 ; y < MAP_HEIGHT ; y++)
	{
		for (x = 0 ; x < MAP_WIDTH ; x++)
		{
			fprintf(fp, "%d ", stage.map[x][y]);
		}
	}

	fclose(fp);

	cJSON_AddStringToObject(root, "map", buff);

	free(buff);
}
#endif

static void saveEntities(cJSON *root)
{
	Entity *e;
	cJSON *entityJSON, *entitiesJSON;

	entitiesJSON = cJSON_CreateArray();

	for (e = stage.entityHead.next ; e != NULL ; e = e->next)
	{
		self = e;

		entityJSON = cJSON_CreateObject();

		cJSON_AddStringToObject(entityJSON, "type", e->typeName);
		cJSON_AddNumberToObject(entityJSON, "x", e->x);
		cJSON_AddNumberToObject(entityJSON, "y", e->y);

		if (strlen(e->name) > 0)
		{
			cJSON_AddStringToObject(entityJSON, "name", e->name);
		}

		if (e->save)
		{
			e->save(entityJSON);
		}

		cJSON_AddItemToArray(entitiesJSON, entityJSON);
	}

	cJSON_AddItemToObject(root, "entities", entitiesJSON);
}

static void saveTips(cJSON *root)
{
	cJSON *tipsJSON;
	int i;

	tipsJSON = cJSON_CreateArray();

	for (i = 0 ; i < MAX_TIPS ; i++)
	{
		if (strlen(stage.tips[i]) > 0)
		{
			cJSON_AddItemToArray(tipsJSON, cJSON_CreateString(stage.tips[i]));
		}
	}

	cJSON_AddItemToObject(root, "tips", tipsJSON);
}

static void saveStage(void)
{
	char filename[MAX_FILENAME_LENGTH], *out;
	cJSON *root;

	sprintf(filename, "data/stages/%03d.json", stage.num);

	printf("Saving %s ...\n", filename);
#ifdef _WIN32
	fflush(stdout);
#endif
	root = cJSON_CreateObject();

	cJSON_AddNumberToObject(root, "cloneLimit", stage.cloneLimit);
	cJSON_AddNumberToObject(root, "timeLimit", stage.timeLimit);

	saveEntities(root);

	saveTips(root);

	saveMap(root);

	out = cJSON_Print(root);

	writeFile(filename, out);

	cJSON_Delete(root);

	free(out);

	printf("Saved %s\n", filename);
#ifdef _WIN32
	fflush(stdout);
#endif
}

static void createEntity(void)
{
	Entity *e;
	int x, y;

	x = (app.mouse.x / 8) * 8;
	y = (app.mouse.y / 8) * 8;

	x += stage.camera.x;
	y += stage.camera.y;

	e = spawnEditorEntity(entity->typeName, x, y);

	addToQuadtree(e, &stage.quadtree);
}

static void deleteEntity(void)
{
	Entity *e, *prev;

	prev = &stage.entityHead;

	for (e = stage.entityHead.next ; e != NULL ; e = e->next)
	{
		if (collision(app.mouse.x + stage.camera.x, app.mouse.y + stage.camera.y, 1, 1, e->x, e->y, e->w, e->h))
		{
			if (e == stage.entityTail)
			{
				stage.entityTail = prev;
			}

			prev->next = e->next;

			removeFromQuadtree(e, &stage.quadtree);

			/* loaded, so safe to delete */
			if (e->id != -1)
			{
				free(e->data);
			}

			free(e);

			e = prev;
		}

		prev = e;
	}
}

static void cycleTile(int dir)
{
	int ok;

	do
	{
		tile += dir;

		if (tile < 0)
		{
			tile = MAX_TILES - 1;
		}
		else if (tile >= MAX_TILES)
		{
			tile = 0;
		}

		ok = stage.tiles[tile] != NULL;
	}
	while (!ok);
}

static void cycleEnt(int dir)
{
	entIndex += dir;

	if (entIndex < 0)
	{
		entIndex = numEnts - 1;
	}
	else if (entIndex >= numEnts)
	{
		entIndex = 0;
	}

	entity = entities[entIndex];
}

static void toggleSelectEntity(void)
{
	Entity *e;
	Platform *p;

	if (selectedEntity == NULL)
	{
		for (e = stage.entityHead.next ; e != NULL ; e = e->next)
		{
			if (collision(app.mouse.x + stage.camera.x, app.mouse.y + stage.camera.y, 1, 1, e->x, e->y, e->w, e->h))
			{
				selectedEntity = e;
				return;
			}
		}
	}
	else
	{
		removeFromQuadtree(selectedEntity, &stage.quadtree);

		selectedEntity->x = ((app.mouse.x / 8) * 8) + stage.camera.x;
		selectedEntity->y = ((app.mouse.y / 8) * 8) + stage.camera.y;

		addToQuadtree(selectedEntity, &stage.quadtree);

		if (strcmp(selectedEntity->typeName, "platform") == 0)
		{
			p = (Platform*)selectedEntity->data;

			p->sx = selectedEntity->x;
			p->sy = selectedEntity->y;
		}

		selectedEntity = NULL;
	}
}

static void flipSelectedEntity(void)
{
	Entity *e;

	if (selectedEntity != NULL)
	{
		selectedEntity->facing = !selectedEntity->facing;
	}
	else
	{
		for (e = stage.entityHead.next ; e != NULL ; e = e->next)
		{
			if (collision(app.mouse.x + stage.camera.x, app.mouse.y + stage.camera.y, 1, 1, e->x, e->y, e->w, e->h))
			{
				e->facing = !e->facing;
				return;
			}
		}
	}
}

static void logic(void)
{
	int x, y;

	if (app.mouse.buttons[SDL_BUTTON_LEFT])
	{
		switch (mode)
		{
			case MODE_TILE:
				x = (app.mouse.x + stage.camera.x) / TILE_SIZE;
				y = (app.mouse.y + stage.camera.y) / TILE_SIZE;
				stage.map[x][y] = tile;
				break;

			case MODE_ENT:
				app.mouse.buttons[SDL_BUTTON_LEFT] = 0;
				createEntity();
				break;

			case MODE_PICK:
				app.mouse.buttons[SDL_BUTTON_LEFT] = 0;
				toggleSelectEntity();
				break;
		}
	}

	if (app.mouse.buttons[SDL_BUTTON_RIGHT])
	{
		switch (mode)
		{
			case MODE_TILE:
				x = (app.mouse.x + stage.camera.x) / TILE_SIZE;
				y = (app.mouse.y + stage.camera.y) / TILE_SIZE;
				stage.map[x][y] = 0;
				break;

			case MODE_ENT:
				deleteEntity();
				break;

			case MODE_PICK:
				app.mouse.buttons[SDL_BUTTON_RIGHT] = 0;
				flipSelectedEntity();
				break;
		}
	}

	if (app.mouse.buttons[SDL_BUTTON_X1])
	{
		app.mouse.buttons[SDL_BUTTON_X1] = 0;

		if (mode == MODE_TILE)
		{
			cycleTile(1);
		}
		else if (mode == MODE_ENT)
		{
			cycleEnt(1);
		}
	}

	if (app.mouse.buttons[SDL_BUTTON_X2])
	{
		app.mouse.buttons[SDL_BUTTON_X2] = 0;

		if (mode == MODE_TILE)
		{
			cycleTile(-1);
		}
		else if (mode == MODE_ENT)
		{
			cycleEnt(-1);
		}
	}

	if (app.keyboard[SDL_SCANCODE_SPACE])
	{
		app.keyboard[SDL_SCANCODE_SPACE] = 0;

		saveStage();
	}

	if (app.keyboard[SDL_SCANCODE_1])
	{
		app.keyboard[SDL_SCANCODE_1] = 0;

		mode = MODE_TILE;
	}

	if (app.keyboard[SDL_SCANCODE_2])
	{
		app.keyboard[SDL_SCANCODE_2] = 0;

		mode = MODE_ENT;
	}

	if (app.keyboard[SDL_SCANCODE_3])
	{
		app.keyboard[SDL_SCANCODE_3] = 0;

		mode = MODE_PICK;
	}

	if (--cameraTimer <= 0)
	{
		cameraTimer = 3;

		if (app.keyboard[SDL_SCANCODE_UP])
		{
			stage.camera.y -= TILE_SIZE;
		}

		if (app.keyboard[SDL_SCANCODE_DOWN])
		{
			stage.camera.y += TILE_SIZE;
		}

		if (app.keyboard[SDL_SCANCODE_LEFT])
		{
			stage.camera.x -= TILE_SIZE;
		}

		if (app.keyboard[SDL_SCANCODE_RIGHT])
		{
			stage.camera.x += TILE_SIZE;
		}

		/* use 64, so things don't look wonky on the right-hand side */
		stage.camera.x = MIN(MAX(stage.camera.x, 0), (MAP_WIDTH * TILE_SIZE) - SCREEN_WIDTH + (TILE_SIZE - 64));
		stage.camera.y = MIN(MAX(stage.camera.y, 0), (MAP_HEIGHT * TILE_SIZE) - SCREEN_HEIGHT);
	}
}

static void drawCurrentTile(void)
{
	int x, y;
	SDL_Rect r;

	x = (app.mouse.x / TILE_SIZE) * TILE_SIZE;
	y = (app.mouse.y / TILE_SIZE) * TILE_SIZE;

	blitAtlasImage(stage.tiles[tile], x, y, 0, SDL_FLIP_NONE);

	r.x = x;
	r.y = y;
	r.w = TILE_SIZE;
	r.h = TILE_SIZE;

	SDL_SetRenderDrawColor(app.renderer, 255, 255, 0, 255);
	SDL_RenderDrawRect(app.renderer, &r);
}

static void drawCurrentEnt(void)
{
	int x, y;

	x = (app.mouse.x / 8) * 8;
	y = (app.mouse.y / 8) * 8;

	blitAtlasImage(entity->atlasImage, x, y, 0, SDL_FLIP_NONE);
}

static void drawSelectedEnt(void)
{
	int x, y;

	if (selectedEntity != NULL)
	{
		x = (app.mouse.x / 8) * 8;
		y = (app.mouse.y / 8) * 8;

		removeFromQuadtree(selectedEntity, &stage.quadtree);

		selectedEntity->x = x + stage.camera.x;
		selectedEntity->y = y + stage.camera.y;

		addToQuadtree(selectedEntity, &stage.quadtree);
	}
}

static void drawInfo(void)
{
	Entity *e;
	int x, y;

	x = ((app.mouse.x + stage.camera.x) / TILE_SIZE) * TILE_SIZE;
	y = ((app.mouse.y + stage.camera.y) / TILE_SIZE) * TILE_SIZE;

	drawRect(0, 0, SCREEN_WIDTH, 30, 0, 0, 0, 192);

	drawText(10, 0, 32, TEXT_LEFT, app.colors.white, "Stage: %d", stage.num);

	drawText(310, 0, 32, TEXT_LEFT, app.colors.white, "Pos: %d,%d", x, y);

	if (mode == MODE_PICK)
	{
		for (e = stage.entityHead.next ; e != NULL ; e = e->next)
		{
			if (collision(app.mouse.x + stage.camera.x, app.mouse.y + stage.camera.y, 1, 1, e->x, e->y, e->w, e->h))
			{
				drawText(e->x + (e->w / 2) - stage.camera.x, e->y - 32 - stage.camera.y, 32, TEXT_CENTER, app.colors.white, "%d,%d", (int)e->x, (int)e->y);
			}
		}
	}
}

static void draw(void)
{
	drawMap();

	drawEntities(0);
	drawEntities(1);

	switch (mode)
	{
		case MODE_TILE:
			drawCurrentTile();
			break;

		case MODE_ENT:
			drawCurrentEnt();
			break;

		case MODE_PICK:
			drawSelectedEnt();
			break;
	}

	drawInfo();
}

static void tryLoadStage(void)
{
	Entity *e;
	char filename[MAX_FILENAME_LENGTH];

	sprintf(filename, "data/stages/%03d.json", stage.num);

	stage.timeLimit = 3600;

	if (fileExists(filename))
	{
		loadStage(0);

		for (e = stage.entityHead.next ; e != NULL ; e = e->next)
		{
			addToQuadtree(e, &stage.quadtree);
		}
	}
}

/* defaults */
static void loadTiles(void)
{
	int i;
	char filename[MAX_FILENAME_LENGTH];

	for (i = 1 ; i <= MAX_TILES ; i++)
	{
		sprintf(filename, "gfx/tilesets/brick/%d.png", i);

		stage.tiles[i] = getAtlasImage(filename, 0);
	}
}

static void centreOnPlayer(void)
{
	Entity *e;

	for (e = stage.entityHead.next ; e != NULL ; e = e->next)
	{
		if (e->type == ET_PLAYER)
		{
			stage.camera.x = e->x;
			stage.camera.y = e->y;

			stage.camera.x -= SCREEN_WIDTH / 2;
			stage.camera.y -= SCREEN_HEIGHT / 2;
		}

		e->flags &= ~EF_INVISIBLE;
	}
}

/*static void handleCommandLine(int argc, char *argv[])
{
	int i;

	for (i = 1 ; i < argc ; i++)
	{
		if (strcmp(argv[i], "-stage") == 0)
		{
			stage.num = atoi(argv[i + 1]);
		}
	}
}

int main(int argc, char *argv[])
{
	long then;
	float remainder;

	memset(&app, 0, sizeof(App));
	app.texturesTail = &app.texturesHead;

	tile = 1;
	cameraTimer = 0;
	mode = MODE_TILE;
	entIndex = 0;
	selectedEntity = NULL;

	initSDL();

	atexit(cleanup);

	SDL_ShowCursor(1);

	initGame();

	memset(&stage, 0, sizeof(Stage));
	stage.entityTail = &stage.entityHead;

	handleCommandLine(argc, argv);

	entities = initAllEnts(&numEnts);
	entity = entities[0];

	loadTiles();

	tryLoadStage();

	centreOnPlayer();

	then = SDL_GetTicks();

	remainder = 0;

	while (1)
	{
		prepareScene();

		doInput();

		logic();

		draw();

		presentScene();

		capFrameRate(&then, &remainder);
	}

	return 0;
}*/

static void capFrameRate(long *then, float *remainder)
{
	long wait, frameTime;

	wait = 16 + *remainder;

	*remainder -= (int)*remainder;

	frameTime = SDL_GetTicks() - *then;

	wait -= frameTime;

	if (wait < 1)
	{
		wait = 1;
	}

	SDL_Delay(wait);

	*remainder += 0.667;

	*then = SDL_GetTicks();
}

typedef struct {
    int *data;
    size_t size;
    size_t capacity;
} IntVector;

void vector_init(IntVector *v) {
    v->size = 0;
    v->capacity = 8;
    v->data = malloc(v->capacity * sizeof(int));
}

void vector_push(IntVector *v, int val) {
    if (v->size >= v->capacity) {
        v->capacity *= 2;
        v->data = realloc(v->data, v->capacity * sizeof(int));
    }
    v->data[v->size++] = val;
}

int vector_max_except(IntVector *v, int exclude) {
    int max = -1;
    for (size_t i = 0; i < v->size; i++) {
        if (v->data[i] != exclude && v->data[i] > max) {
            max = v->data[i];
        }
    }
    return max;
}

void vector_free(IntVector *v) {
    free(v->data);
}

int copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return -1;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        fwrite(buf, 1, n, out);
    }
    fclose(in);
    fclose(out);
    return 0;
}

void load_stages(const char *folder, IntVector *ids) {
    DIR *dir = opendir(folder);
    if (!dir) return;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strstr(ent->d_name, ".json")) {
            char name[256];
            strncpy(name, ent->d_name, sizeof(name));
            name[sizeof(name)-1] = 0;
            char *dot = strchr(name, '.');
            if (dot) *dot = 0;

            int valid = 1;
            for (int i=0; name[i]; i++) {
                if (!isdigit((unsigned char)name[i])) {
                    valid = 0;
                    break;
                }
            }
            if (valid) {
                int n = atoi(name);
                vector_push(ids, n);
            }
        }
    }
    closedir(dir);
}

void draw_text(SDL_Renderer *ren, TTF_Font *font, const char *msg, int x, int y, SDL_Color color) {
    SDL_Surface *surf = TTF_RenderText_Solid(font, msg, color);
    SDL_Texture *tex = SDL_CreateTextureFromSurface(ren, surf);
    SDL_Rect dst = {x, y, surf->w, surf->h};
    SDL_RenderCopy(ren, tex, NULL, &dst);
    SDL_FreeSurface(surf);
    SDL_DestroyTexture(tex);
}

void draw_list(SDL_Renderer *ren, TTF_Font *font, IntVector *ids, int selected, int offset) {
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);

    for (int i=0; i<10 && i+offset<ids->size; i++) {
        int stage = ids->data[i+offset];
        char label[64];
        snprintf(label, sizeof(label), "Stage %d", stage);

        SDL_Color col = (i+offset == selected) ? (SDL_Color){0,200,255} : (SDL_Color){200,200,200};

        SDL_Surface *surf = TTF_RenderText_Solid(font, label, col);
        SDL_Texture *tex = SDL_CreateTextureFromSurface(ren, surf);

        int x = (WINDOW_W - surf->w) / 2;
        int y = 50 + i*40;
        SDL_Rect dst = {x, y, surf->w, surf->h};

        SDL_RenderCopy(ren, tex, NULL, &dst);
        SDL_FreeSurface(surf);
        SDL_DestroyTexture(tex);
    }
    SDL_RenderPresent(ren);
}

int choose_stage(SDL_Renderer *ren, TTF_Font *font, IntVector *stages) {
    int running = 1;
    int selected = 0;
    int offset = 0;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) exit(0);
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_DOWN && selected < (int)stages->size-1) {
                    selected++;
                    if (selected >= offset+10) offset++;
                }
                if (e.key.keysym.sym == SDLK_UP && selected > 0) {
                    selected--;
                    if (selected < offset) offset--;
                }
                if (e.key.keysym.sym == SDLK_RETURN) {
                    running = 0;
                }
            }
        }
        draw_list(ren, font, stages, selected, offset);
        SDL_Delay(16);
    }
    return stages->data[selected];
}

int choose_action(SDL_Renderer *ren, TTF_Font *font) {
    int action = 0;
    int running = 1;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) exit(0);
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_LEFT) action = 0;
                if (e.key.keysym.sym == SDLK_RIGHT) action = 1;
                if (e.key.keysym.sym == SDLK_RETURN) running = 0;
            }
        }

        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);

        SDL_Color col1 = (action==0) ? (SDL_Color){0,200,0} : (SDL_Color){150,150,150};
        SDL_Color col2 = (action==1) ? (SDL_Color){0,200,0} : (SDL_Color){150,150,150};

        draw_text(ren, font, "Edit current Stage", 150, 200, col1);
        draw_text(ren, font, "Clone as new Stage", 350, 200, col2);

        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }
    return action;
}

int main(int argc, char **argv) {
    const char *folder = "data/stages";
    IntVector stages;
    vector_init(&stages);
    load_stages(folder, &stages);

    if (stages.size == 0) {
        printf("No Stage found!\n");
        return 1;
    }
	
    SDL_Init(SDL_INIT_VIDEO);
    TTF_Init();

    SDL_Window *win = SDL_CreateWindow("Map Editor",
                                       SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED,
                                       WINDOW_W, WINDOW_H, 0);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);

    TTF_Font *font = TTF_OpenFont("fonts/EnterCommand.ttf", 24);
    if (!font) {
        printf("Font error: %s\n", TTF_GetError());
        return 1;
    }

    int chosen = choose_stage(ren, font, &stages);
    printf("Chosen Stage: %d\n", chosen);

    int action = choose_action(ren, font);

    if (action == 0) {
        printf("Edit Stage %d\n", chosen);
    } else {
        int max = vector_max_except(&stages, 999);
        int newId = max+1;
        char src[256], dst[256];
        snprintf(src, sizeof(src), "%s/%03d.json", folder, chosen);
        snprintf(dst, sizeof(dst), "%s/%03d.json", folder, newId);
        if (copy_file(src, dst)==0) {
            printf("Stage cloned %d -> Stage %d\n", chosen, newId);
            chosen = newId;
        } else {
            printf("Error cloning selected Stage!\n");
        }
    }

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    TTF_CloseFont(font);
    TTF_Quit();
    SDL_Quit();
    vector_free(&stages);
	
	long then;
	float remainder;

	memset(&app, 0, sizeof(App));
	app.texturesTail = &app.texturesHead;

	tile = 1;
	cameraTimer = 0;
	mode = MODE_TILE;
	entIndex = 0;
	selectedEntity = NULL;

	initSDL();

	atexit(cleanup);

	SDL_ShowCursor(1);

	initGame();

	memset(&stage, 0, sizeof(Stage));
	stage.entityTail = &stage.entityHead;

	stage.num = chosen;

	entities = initAllEnts(&numEnts);
	entity = entities[0];

	loadTiles();

	tryLoadStage();

	centreOnPlayer();

	then = SDL_GetTicks();

	remainder = 0;

	while (1)
	{
		prepareScene();

		doInput();

		logic();

		draw();

		presentScene();

		capFrameRate(&then, &remainder);
	}

	return 0;
}
