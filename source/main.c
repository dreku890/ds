/*
 * Blockcraft DS
 * An original block-building homebrew game for the Nintendo DS (Lite)
 * Built with libnds (devkitPro).
 *
 * Controls:
 *   D-Pad        : move forward/back and strafe
 *   X / B        : fly up / fly down
 *   Stylus drag  : look around (touch screen)
 *   L / R        : turn left / right (alternative to stylus)
 *   A            : place block (at the highlighted spot)
 *   Y            : break block (the highlighted block)
 *   SELECT       : cycle selected block type
 *   START        : reset world
 */

#include <nds.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* World                                                               */
/* ------------------------------------------------------------------ */

#define WX 24
#define WY 10
#define WZ 24

#define BLOCK_AIR    0
#define BLOCK_GRASS  1
#define BLOCK_DIRT   2
#define BLOCK_STONE  3
#define BLOCK_WOOD   4
#define BLOCK_LEAVES 5
#define BLOCK_SAND   6
#define BLOCK_BRICK  7
#define NUM_BLOCK_TYPES 8   /* including air */

static u8 world[WX][WY][WZ];

static const char *blockNames[NUM_BLOCK_TYPES] = {
	"Air", "Grass", "Dirt", "Stone", "Wood", "Leaves", "Sand", "Brick"
};

/* top-face colors (r,g,b 0..31 scaled to 0..255 later) */
typedef struct { u8 r, g, b; } Color;

static const Color blockColors[NUM_BLOCK_TYPES] = {
	{   0,   0,   0 }, /* air (unused) */
	{  90, 180,  70 }, /* grass */
	{ 130,  95,  60 }, /* dirt */
	{ 130, 130, 135 }, /* stone */
	{ 155, 110,  60 }, /* wood */
	{  60, 140,  50 }, /* leaves */
	{ 220, 205, 150 }, /* sand */
	{ 175,  80,  70 }, /* brick */
};

static inline int inWorld(int x, int y, int z)
{
	return x >= 0 && x < WX && y >= 0 && y < WY && z >= 0 && z < WZ;
}

static inline u8 getBlock(int x, int y, int z)
{
	if (!inWorld(x, y, z)) return BLOCK_AIR;
	return world[x][y][z];
}

static void generateWorld(void)
{
	int x, y, z;

	for (x = 0; x < WX; x++) {
		for (z = 0; z < WZ; z++) {
			/* gentle rolling hills from two sine waves */
			float h = 3.5f
				+ 1.6f * sinf(x * 0.45f)
				+ 1.4f * cosf(z * 0.38f)
				+ 0.7f * sinf((x + z) * 0.23f);
			int height = (int)h;
			if (height < 1) height = 1;
			if (height > WY - 3) height = WY - 3;

			for (y = 0; y < WY; y++) {
				if (y < height - 2)      world[x][y][z] = BLOCK_STONE;
				else if (y < height)     world[x][y][z] = BLOCK_DIRT;
				else if (y == height)    world[x][y][z] = (height <= 2) ? BLOCK_SAND : BLOCK_GRASS;
				else                     world[x][y][z] = BLOCK_AIR;
			}
		}
	}

	/* a couple of simple trees on flat-ish grass */
	{
		static const int tx[3] = { 6, 15, 19 };
		static const int tz[3] = { 7, 16, 5 };
		int t;
		for (t = 0; t < 3; t++) {
			int gx = tx[t], gz = tz[t];
			int gy;
			/* find surface */
			for (gy = WY - 1; gy > 0; gy--)
				if (world[gx][gy][gz] != BLOCK_AIR) break;
			if (world[gx][gy][gz] != BLOCK_GRASS) continue;
			if (gy + 4 >= WY) continue;

			world[gx][gy + 1][gz] = BLOCK_WOOD;
			world[gx][gy + 2][gz] = BLOCK_WOOD;

			int lx, ly, lz;
			for (lx = -1; lx <= 1; lx++)
				for (lz = -1; lz <= 1; lz++)
					for (ly = 3; ly <= 4; ly++) {
						int ax = gx + lx, ay = gy + ly, az = gz + lz;
						if (ly == 4 && (lx != 0 || lz != 0)) continue;
						if (inWorld(ax, ay, az) && world[ax][ay][az] == BLOCK_AIR)
							world[ax][ay][az] = BLOCK_LEAVES;
					}
		}
	}
}

/* ------------------------------------------------------------------ */
/* Camera / player                                                     */
/* ------------------------------------------------------------------ */

static float px, py, pz;      /* player position  */
static float yaw, pitch;      /* radians          */

static void resetPlayer(void)
{
	px = WX / 2.0f;
	py = 7.5f;
	pz = WZ / 2.0f;
	yaw = 0.8f;
	pitch = -0.3f;
}

/* ------------------------------------------------------------------ */
/* Raycast: find the block we are looking at                           */
/* ------------------------------------------------------------------ */

typedef struct {
	int hit;                 /* did we hit a solid block? */
	int bx, by, bz;          /* block that was hit        */
	int nx, ny, nz;          /* block in front of the hit face (for placing) */
} RayHit;

static RayHit raycast(float maxDist)
{
	RayHit r = { 0, 0, 0, 0, 0, 0, 0 };

	float dx = cosf(pitch) * sinf(yaw);
	float dy = sinf(pitch);
	float dz = -cosf(pitch) * cosf(yaw);

	float step = 0.05f;
	float t;
	int lastX = (int)floorf(px);
	int lastY = (int)floorf(py);
	int lastZ = (int)floorf(pz);

	for (t = 0.0f; t < maxDist; t += step) {
		float cx = px + dx * t;
		float cy = py + dy * t;
		float cz = pz + dz * t;
		int bx = (int)floorf(cx);
		int by = (int)floorf(cy);
		int bz = (int)floorf(cz);

		if (bx == lastX && by == lastY && bz == lastZ) continue;

		if (getBlock(bx, by, bz) != BLOCK_AIR) {
			r.hit = 1;
			r.bx = bx; r.by = by; r.bz = bz;
			r.nx = lastX; r.ny = lastY; r.nz = lastZ;
			return r;
		}
		lastX = bx; lastY = by; lastZ = bz;
	}
	return r;
}

/* ------------------------------------------------------------------ */
/* Rendering                                                           */
/* ------------------------------------------------------------------ */

/* shade a color by a percentage (0..100) and submit it */
static inline void shadedColor(Color c, int percent)
{
	glColor3b((u8)(c.r * percent / 100),
	          (u8)(c.g * percent / 100),
	          (u8)(c.b * percent / 100));
}

/*
 * Draw one cube at integer position (x,y,z), drawing only faces
 * that are exposed to air. Faces use flat colors with per-face
 * shading so the geometry reads clearly without textures.
 */
static void drawBlock(int x, int y, int z, u8 type, int highlighted)
{
	Color c = blockColors[type];
	/* grass has dirt-colored sides */
	Color side = (type == BLOCK_GRASS) ? blockColors[BLOCK_DIRT] : c;

	if (highlighted) {
		c.r = (u8)((c.r + 255) / 2);
		c.g = (u8)((c.g + 255) / 2);
		c.b = (u8)((c.b + 255) / 2);
		side = c;
	}

	float x0 = (float)x,     y0 = (float)y,     z0 = (float)z;
	float x1 = (float)x + 1, y1 = (float)y + 1, z1 = (float)z + 1;

	glBegin(GL_QUADS);

	/* top (+y) — brightest */
	if (getBlock(x, y + 1, z) == BLOCK_AIR) {
		shadedColor(c, 100);
		glVertex3f(x0, y1, z0);
		glVertex3f(x0, y1, z1);
		glVertex3f(x1, y1, z1);
		glVertex3f(x1, y1, z0);
	}
	/* bottom (-y) — darkest */
	if (getBlock(x, y - 1, z) == BLOCK_AIR) {
		shadedColor(side, 45);
		glVertex3f(x0, y0, z0);
		glVertex3f(x1, y0, z0);
		glVertex3f(x1, y0, z1);
		glVertex3f(x0, y0, z1);
	}
	/* north (-z) */
	if (getBlock(x, y, z - 1) == BLOCK_AIR) {
		shadedColor(side, 80);
		glVertex3f(x0, y0, z0);
		glVertex3f(x0, y1, z0);
		glVertex3f(x1, y1, z0);
		glVertex3f(x1, y0, z0);
	}
	/* south (+z) */
	if (getBlock(x, y, z + 1) == BLOCK_AIR) {
		shadedColor(side, 80);
		glVertex3f(x1, y0, z1);
		glVertex3f(x1, y1, z1);
		glVertex3f(x0, y1, z1);
		glVertex3f(x0, y0, z1);
	}
	/* west (-x) */
	if (getBlock(x - 1, y, z) == BLOCK_AIR) {
		shadedColor(side, 65);
		glVertex3f(x0, y0, z1);
		glVertex3f(x0, y1, z1);
		glVertex3f(x0, y1, z0);
		glVertex3f(x0, y0, z0);
	}
	/* east (+x) */
	if (getBlock(x + 1, y, z) == BLOCK_AIR) {
		shadedColor(side, 65);
		glVertex3f(x1, y0, z0);
		glVertex3f(x1, y1, z0);
		glVertex3f(x1, y1, z1);
		glVertex3f(x1, y0, z1);
	}

	glEnd();
}

#define DRAW_RADIUS 9   /* blocks drawn around the player (poly budget) */

static void drawWorld(const RayHit *look)
{
	int cx = (int)floorf(px);
	int cz = (int)floorf(pz);

	int x0 = cx - DRAW_RADIUS; if (x0 < 0) x0 = 0;
	int x1 = cx + DRAW_RADIUS; if (x1 >= WX) x1 = WX - 1;
	int z0 = cz - DRAW_RADIUS; if (z0 < 0) z0 = 0;
	int z1 = cz + DRAW_RADIUS; if (z1 >= WZ) z1 = WZ - 1;

	int x, y, z;
	for (x = x0; x <= x1; x++)
		for (z = z0; z <= z1; z++)
			for (y = 0; y < WY; y++) {
				u8 b = world[x][y][z];
				if (b == BLOCK_AIR) continue;
				int hl = look->hit &&
				         look->bx == x && look->by == y && look->bz == z;
				drawBlock(x, y, z, b, hl);
			}
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
	/* 3D on the top screen, console on the bottom screen */
	videoSetMode(MODE_0_3D);
	consoleDemoInit();

	glInit();
	glEnable(GL_ANTIALIAS);

	/* sky color */
	glClearColor(13, 22, 30, 31);
	glClearPolyID(63);
	glClearDepth(0x7FFF);

	glViewport(0, 0, 255, 191);

	generateWorld();
	resetPlayer();

	int selected = BLOCK_STONE;
	int prevTouchValid = 0;
	touchPosition prevTouch = { 0 };

	iprintf("\x1b[2J");
	iprintf("  BLOCKCRAFT DS\n");
	iprintf("  ----------------------------\n");
	iprintf("  DPad: move   X/B: up/down\n");
	iprintf("  Stylus drag or L/R: look\n");
	iprintf("  A: place   Y: break\n");
	iprintf("  SELECT: block  START: reset\n");

	while (1) {
		scanKeys();
		int held = keysHeld();
		int down = keysDown();

		/* ---- look: stylus drag ---- */
		if (held & KEY_TOUCH) {
			touchPosition t;
			touchRead(&t);
			if (prevTouchValid) {
				yaw   += (t.px - prevTouch.px) * 0.012f;
				pitch -= (t.py - prevTouch.py) * 0.012f;
			}
			prevTouch = t;
			prevTouchValid = 1;
		} else {
			prevTouchValid = 0;
		}

		/* ---- look: shoulder buttons ---- */
		if (held & KEY_L) yaw -= 0.045f;
		if (held & KEY_R) yaw += 0.045f;

		if (pitch >  1.45f) pitch =  1.45f;
		if (pitch < -1.45f) pitch = -1.45f;

		/* ---- movement (fly mode) ---- */
		{
			float speed = 0.12f;
			float fx = sinf(yaw), fz = -cosf(yaw);

			if (held & KEY_UP)    { px += fx * speed; pz += fz * speed; }
			if (held & KEY_DOWN)  { px -= fx * speed; pz -= fz * speed; }
			if (held & KEY_LEFT)  { px += fz * speed; pz -= fx * speed; }
			if (held & KEY_RIGHT) { px -= fz * speed; pz += fx * speed; }
			if (held & KEY_X)     py += speed;
			if (held & KEY_B)     py -= speed;

			if (px < 0.5f) px = 0.5f;         if (px > WX - 0.5f) px = WX - 0.5f;
			if (pz < 0.5f) pz = 0.5f;         if (pz > WZ - 0.5f) pz = WZ - 0.5f;
			if (py < 0.5f) py = 0.5f;         if (py > WY + 6.0f) py = WY + 6.0f;
		}

		/* ---- block interaction ---- */
		RayHit look = raycast(5.0f);

		if (down & KEY_SELECT) {
			selected++;
			if (selected >= NUM_BLOCK_TYPES) selected = 1;
		}
		if ((down & KEY_Y) && look.hit) {
			world[look.bx][look.by][look.bz] = BLOCK_AIR;
		}
		if ((down & KEY_A) && look.hit && inWorld(look.nx, look.ny, look.nz)) {
			/* don't place a block inside the player */
			int pxi = (int)floorf(px), pyi = (int)floorf(py), pzi = (int)floorf(pz);
			if (!(look.nx == pxi && look.ny == pyi && look.nz == pzi))
				world[look.nx][look.ny][look.nz] = selected;
		}
		if (down & KEY_START) {
			generateWorld();
			resetPlayer();
		}

		/* ---- HUD on the bottom screen ---- */
		iprintf("\x1b[8;0H");
		iprintf("  Block: %-8s            \n", blockNames[selected]);
		iprintf("  Pos: %2d,%2d,%2d              \n",
		        (int)px, (int)py, (int)pz);
		iprintf("  Aim: %s                   \n",
		        look.hit ? blockNames[getBlock(look.bx, look.by, look.bz)]
		                 : "-");

		/* ---- render ---- */
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		gluPerspective(70, 256.0f / 192.0f, 0.1f, 40.0f);

		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		{
			float lx = px + cosf(pitch) * sinf(yaw);
			float ly = py + sinf(pitch);
			float lz = pz - cosf(pitch) * cosf(yaw);
			gluLookAt(px, py, pz, lx, ly, lz, 0.0f, 1.0f, 0.0f);
		}

		glPolyFmt(POLY_ALPHA(31) | POLY_CULL_BACK);

		drawWorld(&look);

		glFlush(0);
		swiWaitForVBlank();
	}

	return 0;
}
