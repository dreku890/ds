/*
 * Blockcraft DS
 * An original block-building survival homebrew game for the Nintendo DS (Lite)
 * Built with libnds (devkitPro).
 *
 * Features:
 *  - 64 x 16 x 64 block world with hills, water, beaches, trees and flowers
 *  - Survival movement: gravity, jumping, collision (plus a fly mode toggle)
 *  - Break blocks to collect them, place them from your inventory
 *  - Wandering animals (sheep and pigs) with simple AI
 *  - Day / night cycle
 *
 * Controls:
 *   D-Pad        : move forward/back and strafe
 *   A            : jump  (in fly mode: fly up)
 *   B            : swing sword (walk mode) / fly down (fly mode)
 *   L (hold)     : break the highlighted block
 *   L+B          : craft a sword (costs 2 planks)
 *   R            : place the selected block
 *   Stylus drag  : look around (touch screen)
 *   SELECT       : cycle selected inventory slot
 *   X            : toggle fly mode
 *   Y            : toggle HUD page (inventory / info)
 *   START        : regenerate the world
 *   L+R+START    : save world to SD card (auto-loads on next boot)
 *
 * Survival:
 *   Hostile mobs (zombies) spawn at night and chase the player.
 *   Contact deals 1 damage (5 HP max). On death the player respawns.
 *   Mobs despawn automatically at dawn.
 */

#include <nds.h>
#include <fat.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* World                                                               */
/* ------------------------------------------------------------------ */

#define WX 64
#define WY 16
#define WZ 64

#define WATER_LEVEL 4

enum {
	BLOCK_AIR = 0,
	BLOCK_GRASS,
	BLOCK_DIRT,
	BLOCK_STONE,
	BLOCK_WOOD,
	BLOCK_LEAVES,
	BLOCK_SAND,
	BLOCK_BRICK,
	BLOCK_PLANKS,
	BLOCK_COBBLE,
	BLOCK_FLOWER,
	BLOCK_WATER,
	NUM_BLOCK_TYPES
};

static u8 world[WX][WY][WZ];

static const char *blockNames[NUM_BLOCK_TYPES] = {
	"Air", "Grass", "Dirt", "Stone", "Wood", "Leaves",
	"Sand", "Brick", "Planks", "Cobble", "Flower", "Water"
};

typedef struct { u8 r, g, b; } Color;

static const Color blockColors[NUM_BLOCK_TYPES] = {
	{   0,   0,   0 }, /* air (unused)  */
	{  90, 180,  70 }, /* grass         */
	{ 130,  95,  60 }, /* dirt          */
	{ 130, 130, 135 }, /* stone         */
	{ 120,  85,  45 }, /* wood          */
	{  55, 135,  45 }, /* leaves        */
	{ 220, 205, 150 }, /* sand          */
	{ 175,  80,  70 }, /* brick         */
	{ 190, 155,  95 }, /* planks        */
	{ 105, 105, 105 }, /* cobblestone   */
	{ 235, 200,  60 }, /* flower        */
	{  50,  90, 200 }, /* water         */
};

/* blocks the player can walk through */
static inline int isPassable(u8 b)
{
	return b == BLOCK_AIR || b == BLOCK_WATER || b == BLOCK_FLOWER;
}

/* blocks that can be collected / placed from inventory */
static inline int isCollectable(u8 b)
{
	return b != BLOCK_AIR && b != BLOCK_WATER;
}

static inline int inWorld(int x, int y, int z)
{
	return x >= 0 && x < WX && y >= 0 && y < WY && z >= 0 && z < WZ;
}

static inline u8 getBlock(int x, int y, int z)
{
	if (!inWorld(x, y, z)) return BLOCK_AIR;
	return world[x][y][z];
}

/* ------------------------------------------------------------------ */
/* Terrain generation (hash-based value noise)                         */
/* ------------------------------------------------------------------ */

static u32 worldSeed = 1337;

static u32 hash2(int x, int z)
{
	u32 h = worldSeed;
	h ^= (u32)x * 374761393u;
	h ^= (u32)z * 668265263u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return h ^ (h >> 16);
}

/* noise value 0..1 at integer lattice point */
static float latNoise(int x, int z)
{
	return (hash2(x, z) & 0xFFFF) / 65535.0f;
}

static float smoothNoise(float x, float z)
{
	int ix = (int)floorf(x), iz = (int)floorf(z);
	float fx = x - ix, fz = z - iz;
	/* smoothstep */
	fx = fx * fx * (3.0f - 2.0f * fx);
	fz = fz * fz * (3.0f - 2.0f * fz);

	float a = latNoise(ix,     iz);
	float b = latNoise(ix + 1, iz);
	float c = latNoise(ix,     iz + 1);
	float d = latNoise(ix + 1, iz + 1);

	return a + (b - a) * fx + (c - a) * fz + (a - b - c + d) * fx * fz;
}

static int terrainHeight(int x, int z)
{
	float n = 0.0f;
	n += smoothNoise(x * 0.05f,  z * 0.05f)  * 6.5f;  /* large hills   */
	n += smoothNoise(x * 0.15f,  z * 0.15f)  * 2.5f;  /* medium bumps  */
	n += smoothNoise(x * 0.45f,  z * 0.45f)  * 1.0f;  /* small detail  */
	int h = 1 + (int)n;
	if (h < 1) h = 1;
	if (h > WY - 4) h = WY - 4;
	return h;
}

static void plantTree(int gx, int gy, int gz)
{
	int trunk = 2 + (hash2(gx * 7, gz * 3) % 2);   /* 2 or 3 tall */
	int i, lx, ly, lz;

	if (gy + trunk + 2 >= WY) return;

	for (i = 1; i <= trunk; i++)
		world[gx][gy + i][gz] = BLOCK_WOOD;

	for (lx = -2; lx <= 2; lx++)
		for (lz = -2; lz <= 2; lz++)
			for (ly = trunk; ly <= trunk + 2; ly++) {
				int ax = gx + lx, ay = gy + ly, az = gz + lz;
				int ring = abs(lx) + abs(lz);
				if (ly == trunk + 2 && ring > 1) continue; /* rounded top */
				if (ring > 3) continue;
				if (ring == 0 && ly < trunk + 2) continue; /* trunk space */
				if (inWorld(ax, ay, az) && world[ax][ay][az] == BLOCK_AIR)
					world[ax][ay][az] = BLOCK_LEAVES;
			}
}

static void generateWorld(void)
{
	int x, y, z;

	for (x = 0; x < WX; x++)
		for (z = 0; z < WZ; z++) {
			int h = terrainHeight(x, z);

			for (y = 0; y < WY; y++) {
				if (y < h - 2)       world[x][y][z] = BLOCK_STONE;
				else if (y < h)      world[x][y][z] = BLOCK_DIRT;
				else if (y == h) {
					if (h <= WATER_LEVEL + 1) world[x][y][z] = BLOCK_SAND;
					else                      world[x][y][z] = BLOCK_GRASS;
				}
				else if (y <= WATER_LEVEL)  world[x][y][z] = BLOCK_WATER;
				else                 world[x][y][z] = BLOCK_AIR;
			}
		}

	/* decoration pass: trees and flowers on grass */
	for (x = 2; x < WX - 2; x++)
		for (z = 2; z < WZ - 2; z++) {
			int h = terrainHeight(x, z);
			if (world[x][h][z] != BLOCK_GRASS) continue;

			u32 r = hash2(x * 13 + 5, z * 17 + 9);
			if ((r % 97) < 2)                     /* ~2% trees   */
				plantTree(x, h, z);
			else if ((r % 89) < 3 &&              /* ~3% flowers */
			         world[x][h + 1][z] == BLOCK_AIR)
				world[x][h + 1][z] = BLOCK_FLOWER;
		}
}

/* ------------------------------------------------------------------ */
/* Player                                                              */
/* ------------------------------------------------------------------ */

#define EYE_HEIGHT   1.62f
#define PLAYER_H     1.8f
#define PLAYER_R     0.3f
#define GRAVITY      0.014f
#define JUMP_V       0.185f
#define WALK_SPEED   0.085f
#define FLY_SPEED    0.16f

static float px, py, pz;      /* feet position     */
static float vy;              /* vertical velocity */
static float yaw, pitch;
static int   flying;
static int   onGround;

static int inventory[NUM_BLOCK_TYPES];
static int selected = BLOCK_PLANKS;

static int s_fatOk = 0;   /* 1 if fatInitDefault() succeeded; used by HUD and save/load */

/* ---- sword (non-block melee weapon) ---- */
#define SWORD_REACH         1.5f  /* blocks, horizontal kill radius   */
#define SWORD_VERT_REACH    1.8f  /* blocks, vertical kill range      */
#define SWORD_CRAFT_PLANKS  2     /* planks consumed to craft one     */
#define SWING_COOLDOWN      25    /* frames between swings (~0.4 s)   */

static int inventorySword = 0;   /* how many swords the player owns  */
static int swingCooldown  = 0;   /* frames until next swing allowed  */

/* ---- player health ---- */
#define PLAYER_MAX_HP    5
#define HIT_INVINCIBLE   90   /* frames of i-frames after a hit (~1.5 s at 60 fps) */

static int   playerHP         = PLAYER_MAX_HP;
static int   playerInvincible = 0;
static float knockVX          = 0.0f;
static float knockVZ          = 0.0f;

static void resetPlayer(void)
{
	int sx = WX / 2, sz = WZ / 2, sy;
	for (sy = WY - 1; sy > 0; sy--)
		if (!isPassable(world[sx][sy][sz])) break;

	px = sx + 0.5f;
	py = sy + 1.0f;
	pz = sz + 0.5f;
	vy = 0.0f;
	yaw = 0.8f;
	pitch = -0.15f;
	flying = 0;
	onGround = 0;

	{
		int i;
		for (i = 0; i < NUM_BLOCK_TYPES; i++) inventory[i] = 0;
		inventory[BLOCK_PLANKS] = 16;   /* starter kit */
		inventory[BLOCK_COBBLE] = 16;
	}
	inventorySword = 0;
	swingCooldown  = 0;
}

static void damagePlayer(float fromX, float fromZ)
{
	if (playerInvincible > 0) return;

	playerHP--;
	playerInvincible = HIT_INVINCIBLE;

	/* knockback away from the attacker */
	float kx = px - fromX, kz = pz - fromZ;
	float len = sqrtf(kx * kx + kz * kz);
	if (len > 0.01f) { kx /= len; kz /= len; }
	knockVX = kx * 0.35f;
	knockVZ = kz * 0.35f;

	if (playerHP <= 0) {
		/* respawn */
		resetPlayer();
		playerHP         = PLAYER_MAX_HP;
		playerInvincible = 60;
		knockVX = 0.0f;
		knockVZ = 0.0f;
	}
}

/* does the player's bounding box (feet at fy) collide with solids? */
static int boxCollides(float fx, float fy, float fz)
{
	int x0 = (int)floorf(fx - PLAYER_R);
	int x1 = (int)floorf(fx + PLAYER_R);
	int z0 = (int)floorf(fz - PLAYER_R);
	int z1 = (int)floorf(fz + PLAYER_R);
	int y0 = (int)floorf(fy);
	int y1 = (int)floorf(fy + PLAYER_H - 0.01f);
	int x, y, z;

	for (x = x0; x <= x1; x++)
		for (y = y0; y <= y1; y++)
			for (z = z0; z <= z1; z++) {
				if (y < 0) return 1;               /* world floor */
				if (!isPassable(getBlock(x, y, z))) return 1;
			}
	return 0;
}

static void movePlayer(float dx, float dy, float dz)
{
	/* axis-separated movement so we slide along walls */
	if (!boxCollides(px + dx, py, pz)) px += dx;
	if (!boxCollides(px, py, pz + dz)) pz += dz;

	if (!boxCollides(px, py + dy, pz)) {
		py += dy;
		onGround = 0;
	} else {
		if (dy < 0.0f) onGround = 1;
		vy = 0.0f;
	}

	if (px < 0.5f) px = 0.5f;
	if (px > WX - 0.5f) px = WX - 0.5f;
	if (pz < 0.5f) pz = 0.5f;
	if (pz > WZ - 0.5f) pz = WZ - 0.5f;
	if (py < 0.0f) { py = 0.0f; vy = 0.0f; onGround = 1; }
	if (py > WY + 8.0f) py = WY + 8.0f;
}

/* ------------------------------------------------------------------ */
/* Raycast                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
	int hit;
	int bx, by, bz;   /* block hit                        */
	int nx, ny, nz;   /* neighbor cell (place target)     */
} RayHit;

static RayHit raycast(float maxDist)
{
	RayHit r = { 0, 0, 0, 0, 0, 0, 0 };

	float ey = py + EYE_HEIGHT;
	float dx = cosf(pitch) * sinf(yaw);
	float dy = sinf(pitch);
	float dz = -cosf(pitch) * cosf(yaw);

	float t, step = 0.04f;
	int lastX = (int)floorf(px);
	int lastY = (int)floorf(ey);
	int lastZ = (int)floorf(pz);

	for (t = 0.0f; t < maxDist; t += step) {
		int bx = (int)floorf(px + dx * t);
		int by = (int)floorf(ey + dy * t);
		int bz = (int)floorf(pz + dz * t);

		if (bx == lastX && by == lastY && bz == lastZ) continue;

		u8 b = getBlock(bx, by, bz);
		if (b != BLOCK_AIR && b != BLOCK_WATER) {
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
/* Animals                                                             */
/* ------------------------------------------------------------------ */

#define NUM_ANIMALS 8

typedef struct {
	float x, y, z;
	float vy;
	float heading;      /* radians          */
	int   kind;         /* 0 = sheep, 1 = pig */
	int   timer;        /* frames until new decision */
	int   walking;
} Animal;

static Animal animals[NUM_ANIMALS];

static void spawnAnimals(void)
{
	int i;
	for (i = 0; i < NUM_ANIMALS; i++) {
		u32 r = hash2(i * 31 + 7, i * 57 + 3);
		int ax = 4 + (r % (WX - 8));
		int az = 4 + ((r >> 8) % (WZ - 8));
		int ay;
		for (ay = WY - 1; ay > 0; ay--)
			if (!isPassable(world[ax][ay][az])) break;

		animals[i].x = ax + 0.5f;
		animals[i].y = ay + 1.0f;
		animals[i].z = az + 0.5f;
		animals[i].vy = 0.0f;
		animals[i].heading = (r % 628) / 100.0f;
		animals[i].kind = (r >> 16) & 1;
		animals[i].timer = 30 + (r % 120);
		animals[i].walking = 1;
	}
}

static int animalCollides(float x, float y, float z)
{
	/* animals are ~0.7 wide, 0.8 tall */
	int bx = (int)floorf(x);
	int bz = (int)floorf(z);
	int y0 = (int)floorf(y);
	if (y0 < 0) return 1;
	return !isPassable(getBlock(bx, y0, bz)) ||
	       !isPassable(getBlock(bx, y0 + 1, bz)) ? 1 : 0;
}

static void updateAnimals(void)
{
	int i;
	for (i = 0; i < NUM_ANIMALS; i++) {
		Animal *a = &animals[i];

		if (--a->timer <= 0) {
			u32 r = hash2((int)(a->x * 16) + i, (int)(a->z * 16) + a->timer);
			a->walking = (r & 3) != 0;                    /* mostly walk */
			a->heading = (r % 628) / 100.0f;
			a->timer = 60 + (r % 180);
		}

		if (a->walking) {
			float sp = 0.03f;
			float nx = a->x + sinf(a->heading) * sp;
			float nz = a->z - cosf(a->heading) * sp;

			if (!animalCollides(nx, a->y, a->z)) a->x = nx;
			else if (!animalCollides(nx, a->y + 1.0f, a->z) && a->vy == 0.0f) {
				a->vy = 0.16f;                            /* hop up a step */
			} else a->heading += 1.6f;

			if (!animalCollides(a->x, a->y, nz)) a->z = nz;
			else if (!animalCollides(a->x, a->y + 1.0f, nz) && a->vy == 0.0f) {
				a->vy = 0.16f;
			} else a->heading -= 1.6f;
		}

		/* gravity */
		a->vy -= GRAVITY;
		if (a->vy < -0.4f) a->vy = -0.4f;
		{
			float ny = a->y + a->vy;
			if (!animalCollides(a->x, ny, a->z)) a->y = ny;
			else a->vy = 0.0f;
		}

		if (a->x < 1.0f) a->x = 1.0f;
		if (a->x > WX - 1.0f) a->x = WX - 1.0f;
		if (a->z < 1.0f) a->z = 1.0f;
		if (a->z > WZ - 1.0f) a->z = WZ - 1.0f;
		if (a->y < 0.0f) a->y = 0.0f;
	}
}

/* ------------------------------------------------------------------ */
/* Hostile mobs (zombies)                                              */
/* ------------------------------------------------------------------ */

static int dayBrightness = 100;   /* 30 (night) .. 100 (noon), percent */
#define DRAW_RADIUS 10            /* horizontal draw distance in blocks */

/* forward declaration – defined in the Rendering section below */
static void drawBox(float x0, float y0, float z0,
                    float x1, float y1, float z1, Color c);

#define NUM_MOBS          4
#define MOB_SPAWN_DIST_MIN 8    /* don't spawn right on top of player  */
#define MOB_SPAWN_DIST_MAX 22   /* don't spawn beyond draw distance    */
#define MOB_CONTACT_DIST   1.1f /* distance that triggers damage        */
#define MOB_SPEED          0.045f
#define MOB_NIGHT_THRESH   42   /* dayBrightness below this = nighttime */
#define MOB_DAWN_THRESH    60   /* above this zombies must despawn      */
#define MOB_FLASH_FRAMES  14   /* flash duration before sword kill despawn */

typedef struct {
	float x, y, z;
	float vy;
	int   alive;
	int   flashTimer;  /* > 0: hit by sword, flashing before despawn */
} HostileMob;

static HostileMob mobs[NUM_MOBS];
static int        mobSpawnTimer = 0;
static u32        mobRandState  = 9999u;

static u32 mobRand(void)
{
	mobRandState = mobRandState * 1664525u + 1013904223u;
	return mobRandState;
}

static void initMobs(void)
{
	int i;
	for (i = 0; i < NUM_MOBS; i++) { mobs[i].alive = 0; mobs[i].flashTimer = 0; }
	mobSpawnTimer = 60;
}

static void trySpawnMob(void)
{
	/* find a free slot */
	int slot = -1, i;
	for (i = 0; i < NUM_MOBS; i++) {
		if (!mobs[i].alive) { slot = i; break; }
	}
	if (slot < 0) return;   /* all slots occupied */

	/* pick a random position around the player at night */
	int attempts;
	for (attempts = 0; attempts < 16; attempts++) {
		int angle_idx = mobRand() % 628;
		float angle   = angle_idx / 100.0f;
		float dist    = MOB_SPAWN_DIST_MIN +
		                (float)(mobRand() % (MOB_SPAWN_DIST_MAX - MOB_SPAWN_DIST_MIN));

		int mx = (int)(px + sinf(angle) * dist);
		int mz = (int)(pz - cosf(angle) * dist);

		if (mx < 2 || mx >= WX - 2 || mz < 2 || mz >= WZ - 2) continue;

		/* find surface */
		int my;
		for (my = WY - 1; my > 0; my--)
			if (!isPassable(world[mx][my][mz])) break;

		/* don't spawn in water or too deep underground */
		if (world[mx][my][mz] == BLOCK_WATER) continue;
		if (my + 1 >= WY) continue;

		mobs[slot].x    = mx + 0.5f;
		mobs[slot].y    = my + 1.0f;
		mobs[slot].z    = mz + 0.5f;
		mobs[slot].vy   = 0.0f;
		mobs[slot].alive = 1;
		return;
	}
}

static void updateMobs(void)
{
	int i;

	/* despawn at dawn */
	if (dayBrightness > MOB_DAWN_THRESH) {
		for (i = 0; i < NUM_MOBS; i++) mobs[i].alive = 0;
		return;
	}

	/* spawn new mobs at night */
	if (dayBrightness < MOB_NIGHT_THRESH) {
		if (--mobSpawnTimer <= 0) {
			trySpawnMob();
			mobSpawnTimer = 120 + (int)(mobRand() % 180);
		}
	}

	/* update alive mobs */
	for (i = 0; i < NUM_MOBS; i++) {
		HostileMob *m = &mobs[i];
		if (!m->alive) continue;

		/* sword-hit flash: count down then despawn */
		if (m->flashTimer > 0) {
			m->flashTimer--;
			if (m->flashTimer == 0) m->alive = 0;
			continue;   /* frozen while dying */
		}

		/* chase the player */
		float dx = px - m->x;
		float dz = pz - m->z;
		float dist = sqrtf(dx * dx + dz * dz);

		if (dist > 0.01f) {
			float nx = m->x + (dx / dist) * MOB_SPEED;
			float nz = m->z + (dz / dist) * MOB_SPEED;

			/* simple step-up: if blocked horizontally, try hopping */
			if (!animalCollides(nx, m->y, m->z))       m->x = nx;
			else if (!animalCollides(nx, m->y + 1.0f, m->z) && m->vy == 0.0f)
				m->vy = 0.16f;

			if (!animalCollides(m->x, m->y, nz))       m->z = nz;
			else if (!animalCollides(m->x, m->y + 1.0f, nz) && m->vy == 0.0f)
				m->vy = 0.16f;
		}

		/* gravity */
		m->vy -= GRAVITY;
		if (m->vy < -0.4f) m->vy = -0.4f;
		{
			float ny = m->y + m->vy;
			if (!animalCollides(m->x, ny, m->z)) m->y = ny;
			else m->vy = 0.0f;
		}

		/* world bounds */
		if (m->x < 1.0f) m->x = 1.0f;
		if (m->x > WX - 1.0f) m->x = WX - 1.0f;
		if (m->z < 1.0f) m->z = 1.0f;
		if (m->z > WZ - 1.0f) m->z = WZ - 1.0f;
		if (m->y < 0.0f) m->y = 0.0f;

		/* contact damage */
		if (dist < MOB_CONTACT_DIST) {
			damagePlayer(m->x, m->z);
		}
	}
}

static void drawMobs(void)
{
	/* zombie: dark green body, darker head */
	static const Color mobBody  = {  40, 120,  50 };
	static const Color mobHead  = {  30,  90,  40 };
	static const Color mobLeg   = {  20,  60,  25 };
	static const Color mobFlash = { 255, 255, 100 };  /* hit flash: bright yellow */

	int cx = (int)floorf(px), cz = (int)floorf(pz);
	int i;

	for (i = 0; i < NUM_MOBS; i++) {
		HostileMob *m = &mobs[i];
		if (!m->alive) continue;

		int ddx = (int)m->x - cx, ddz = (int)m->z - cz;
		if (ddx * ddx + ddz * ddz > DRAW_RADIUS * DRAW_RADIUS) continue;

		float x = m->x, y = m->y, z = m->z;

		/* alternate between flash color and normal each 2 frames when dying */
		int flashing = (m->flashTimer > 0) && ((m->flashTimer / 2) & 1);
		Color body = flashing ? mobFlash : mobBody;
		Color head = flashing ? mobFlash : mobHead;
		Color leg  = flashing ? mobFlash : mobLeg;

		/* body */
		drawBox(x - 0.28f, y + 0.30f, z - 0.15f,
		        x + 0.28f, y + 0.90f, z + 0.15f, body);
		/* head */
		drawBox(x - 0.22f, y + 0.90f, z - 0.22f,
		        x + 0.22f, y + 1.30f, z + 0.22f, head);
		/* legs */
		drawBox(x - 0.18f, y,         z - 0.10f,
		        x - 0.05f, y + 0.30f, z + 0.10f, leg);
		drawBox(x + 0.05f, y,         z - 0.10f,
		        x + 0.18f, y + 0.30f, z + 0.10f, leg);
	}
}

/* ------------------------------------------------------------------ */
/* Rendering                                                           */
/* ------------------------------------------------------------------ */

static inline void shadedColor(Color c, int percent)
{
	int p = percent * dayBrightness / 100;
	glColor3b((u8)(c.r * p / 100),
	          (u8)(c.g * p / 100),
	          (u8)(c.b * p / 100));
}

static void quadY(float x0, float z0, float x1, float z1, float y)
{
	glVertex3f(x0, y, z0);
	glVertex3f(x0, y, z1);
	glVertex3f(x1, y, z1);
	glVertex3f(x1, y, z0);
}

/* draw an axis-aligned box between two corners with face shading */
static void drawBox(float x0, float y0, float z0,
                    float x1, float y1, float z1, Color c)
{
	glBegin(GL_QUADS);
	/* top */
	shadedColor(c, 100);
	glVertex3f(x0, y1, z0); glVertex3f(x0, y1, z1);
	glVertex3f(x1, y1, z1); glVertex3f(x1, y1, z0);
	/* bottom */
	shadedColor(c, 45);
	glVertex3f(x0, y0, z0); glVertex3f(x1, y0, z0);
	glVertex3f(x1, y0, z1); glVertex3f(x0, y0, z1);
	/* -z */
	shadedColor(c, 80);
	glVertex3f(x0, y0, z0); glVertex3f(x0, y1, z0);
	glVertex3f(x1, y1, z0); glVertex3f(x1, y0, z0);
	/* +z */
	glVertex3f(x1, y0, z1); glVertex3f(x1, y1, z1);
	glVertex3f(x0, y1, z1); glVertex3f(x0, y0, z1);
	/* -x */
	shadedColor(c, 65);
	glVertex3f(x0, y0, z1); glVertex3f(x0, y1, z1);
	glVertex3f(x0, y1, z0); glVertex3f(x0, y0, z0);
	/* +x */
	glVertex3f(x1, y0, z0); glVertex3f(x1, y1, z0);
	glVertex3f(x1, y1, z1); glVertex3f(x1, y0, z1);
	glEnd();
}

static void drawBlockCube(int x, int y, int z, u8 type, int highlighted)
{
	Color c = blockColors[type];
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

	if (isPassable(getBlock(x, y + 1, z))) {           /* top    */
		shadedColor(c, 100);
		quadY(x0, z0, x1, z1, y1);
	}
	if (isPassable(getBlock(x, y - 1, z))) {           /* bottom */
		shadedColor(side, 45);
		glVertex3f(x0, y0, z0); glVertex3f(x1, y0, z0);
		glVertex3f(x1, y0, z1); glVertex3f(x0, y0, z1);
	}
	if (isPassable(getBlock(x, y, z - 1))) {           /* -z */
		shadedColor(side, 80);
		glVertex3f(x0, y0, z0); glVertex3f(x0, y1, z0);
		glVertex3f(x1, y1, z0); glVertex3f(x1, y0, z0);
	}
	if (isPassable(getBlock(x, y, z + 1))) {           /* +z */
		shadedColor(side, 80);
		glVertex3f(x1, y0, z1); glVertex3f(x1, y1, z1);
		glVertex3f(x0, y1, z1); glVertex3f(x0, y0, z1);
	}
	if (isPassable(getBlock(x - 1, y, z))) {           /* -x */
		shadedColor(side, 65);
		glVertex3f(x0, y0, z1); glVertex3f(x0, y1, z1);
		glVertex3f(x0, y1, z0); glVertex3f(x0, y0, z0);
	}
	if (isPassable(getBlock(x + 1, y, z))) {           /* +x */
		shadedColor(side, 65);
		glVertex3f(x1, y0, z0); glVertex3f(x1, y1, z0);
		glVertex3f(x1, y1, z1); glVertex3f(x1, y0, z1);
	}

	glEnd();
}

/* water: just the surface quad, semi-dark */
static void drawWater(int x, int y, int z)
{
	if (getBlock(x, y + 1, z) != BLOCK_AIR) return;
	glBegin(GL_QUADS);
	shadedColor(blockColors[BLOCK_WATER], 90);
	quadY((float)x, (float)z, (float)x + 1, (float)z + 1, (float)y + 0.85f);
	glEnd();
}

/* flower: small cross-ish post */
static void drawFlower(int x, int y, int z)
{
	float cx = x + 0.5f, cz = z + 0.5f;
	drawBox(cx - 0.06f, (float)y, cz - 0.06f,
	        cx + 0.06f, (float)y + 0.4f, cz + 0.06f,
	        blockColors[BLOCK_LEAVES]);
	drawBox(cx - 0.16f, (float)y + 0.4f, cz - 0.16f,
	        cx + 0.16f, (float)y + 0.65f, cz + 0.16f,
	        blockColors[BLOCK_FLOWER]);
}

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
		for (z = z0; z <= z1; z++) {
			/* cheap circular cull to save polygons in the corners */
			int dx = x - cx, dz = z - cz;
			if (dx * dx + dz * dz > DRAW_RADIUS * DRAW_RADIUS + 8) continue;

			for (y = 0; y < WY; y++) {
				u8 b = world[x][y][z];
				if (b == BLOCK_AIR) continue;
				if (b == BLOCK_WATER)      { drawWater(x, y, z);  continue; }
				if (b == BLOCK_FLOWER)     { drawFlower(x, y, z); continue; }

				int hl = look->hit &&
				         look->bx == x && look->by == y && look->bz == z;
				drawBlockCube(x, y, z, b, hl);
			}
		}
}

static void drawAnimals(void)
{
	static const Color sheepBody = { 235, 235, 235 };
	static const Color sheepHead = { 200, 190, 180 };
	static const Color pigBody   = { 240, 150, 160 };
	static const Color pigHead   = { 250, 170, 180 };
	static const Color legColor  = {  90,  70,  55 };

	int cx = (int)floorf(px), cz = (int)floorf(pz);
	int i;

	for (i = 0; i < NUM_ANIMALS; i++) {
		Animal *a = &animals[i];

		int dx = (int)a->x - cx, dz = (int)a->z - cz;
		if (dx * dx + dz * dz > DRAW_RADIUS * DRAW_RADIUS) continue;

		Color body = a->kind ? pigBody : sheepBody;
		Color head = a->kind ? pigHead : sheepHead;

		float hx = sinf(a->heading), hz = -cosf(a->heading);
		float x = a->x, y = a->y, z = a->z;

		/* body */
		drawBox(x - 0.30f, y + 0.25f, z - 0.30f,
		        x + 0.30f, y + 0.70f, z + 0.30f, body);
		/* head, offset in heading direction */
		drawBox(x + hx * 0.38f - 0.15f, y + 0.45f, z + hz * 0.38f - 0.15f,
		        x + hx * 0.38f + 0.15f, y + 0.80f, z + hz * 0.38f + 0.15f,
		        head);
		/* legs */
		drawBox(x - 0.22f, y, z - 0.22f, x - 0.10f, y + 0.25f, z - 0.10f, legColor);
		drawBox(x + 0.10f, y, z - 0.22f, x + 0.22f, y + 0.25f, z - 0.10f, legColor);
		drawBox(x - 0.22f, y, z + 0.10f, x - 0.10f, y + 0.25f, z + 0.22f, legColor);
		drawBox(x + 0.10f, y, z + 0.10f, x + 0.22f, y + 0.25f, z + 0.22f, legColor);
	}
}

/* ------------------------------------------------------------------ */
/* Day / night cycle                                                   */
/* ------------------------------------------------------------------ */

#define DAY_LENGTH 3600   /* frames per full day (~60s at 60fps) */

static int timeOfDay = DAY_LENGTH / 4;   /* start at morning */

static void updateDayNight(void)
{
	timeOfDay = (timeOfDay + 1) % DAY_LENGTH;

	/* brightness curve: bright at midday, dark at midnight */
	float phase = (float)timeOfDay / DAY_LENGTH * 2.0f * (float)M_PI;
	float b = 0.65f - 0.35f * cosf(phase);          /* 0.30 .. 1.00 */
	dayBrightness = (int)(b * 100.0f);

	/* sky color follows brightness */
	int sr = (int)( 8.0f * b) + 2;
	int sg = (int)(16.0f * b) + 2;
	int sb = (int)(24.0f * b) + 4;
	if (sr > 31) sr = 31;
	if (sg > 31) sg = 31;
	if (sb > 31) sb = 31;
	glClearColor(sr, sg, sb, 31);
}

/* ------------------------------------------------------------------ */
/* HUD (bottom screen console)                                         */
/* ------------------------------------------------------------------ */

static int hudPage = 0;

static void drawHUD(const RayHit *look)
{
	/* build health string: filled hearts (*) and empty (-) */
	char hpStr[PLAYER_MAX_HP + 1];
	{
		int h;
		for (h = 0; h < PLAYER_MAX_HP; h++)
			hpStr[h] = (h < playerHP) ? '*' : '-';
		hpStr[PLAYER_MAX_HP] = '\0';
	}

	iprintf("\x1b[0;0H");
	iprintf("  BLOCKCRAFT DS  %s  \n", flying ? "[FLY] " : "      ");
	iprintf("  --------------------------\n");
	iprintf("  HP: %s%s\n",
	        hpStr,
	        (playerInvincible > 0 && (playerInvincible / 6) & 1) ? " !" : "  ");

	if (hudPage == 0) {
		int i;
		iprintf("  INVENTORY  (SELECT: next)  \n\n");
		/* sword (non-block item) shown at top of inventory */
		iprintf("    Sword    x%-3d  [B swing] \n", inventorySword);
		for (i = 1; i < NUM_BLOCK_TYPES; i++) {
			if (i == BLOCK_WATER) { iprintf("                            \n"); continue; }
			iprintf("  %c %-8s x%-3d          \n",
			        (i == selected) ? '>' : ' ',
			        blockNames[i], inventory[i]);
		}
	} else {
		iprintf("  INFO                       \n\n");
		iprintf("  Pos:  %2d,%2d,%2d             \n",
		        (int)px, (int)py, (int)pz);
		iprintf("  Aim:  %-8s               \n",
		        look->hit ? blockNames[getBlock(look->bx, look->by, look->bz)]
		                  : "-");
		iprintf("  Time: %s                  \n",
		        dayBrightness > MOB_DAWN_THRESH  ? "Day  " :
		        dayBrightness > MOB_NIGHT_THRESH ? "Dusk " : "Night");
		iprintf("\n  DPad move  A jump          \n");
		iprintf("  B swing    L break         \n");
		iprintf("  R place    L+B craft sword \n");
		iprintf("  Stylus look  X fly         \n");
		iprintf("  Y hud  START new world     \n");
		iprintf("  L+R+START  save world      \n");
		iprintf("  SD card:   %-3s             \n",
		        s_fatOk ? "OK" : "NO");
		iprintf("\n\n\n");
	}
}

/* ------------------------------------------------------------------ */
/* Save / Load  (libfat)                                               */
/* ------------------------------------------------------------------ */

#define SAVE_PATH  "/blockcraft.sav"
#define SAVE_MAGIC 0x42434453u   /* 'BCDS' */
#define SAVE_VER   2u            /* bumped when sword field added */

typedef struct {
	u32   magic;
	u32   version;
	u8    world_data[WX * WY * WZ];   /* 65 536 bytes */
	float px, py, pz;
	float vy, yaw, pitch;
	s32   flying;
	s32   selected;
	s32   inventory[NUM_BLOCK_TYPES];
	s32   timeOfDay;
	s32   inventorySword;
} SaveFile;

/* kept static so it lives in BSS, not on the tiny ARM9 stack */
static SaveFile s_saveBuf;

static void initFAT(void)
{
	s_fatOk = fatInitDefault();
}

/* Returns 1 on success, 0 on any failure (prints nothing itself). */
static int saveWorld(void)
{
	if (!s_fatOk) return 0;

	s_saveBuf.magic   = SAVE_MAGIC;
	s_saveBuf.version = SAVE_VER;

	{
		int x, y, z, idx = 0;
		for (x = 0; x < WX; x++)
			for (y = 0; y < WY; y++)
				for (z = 0; z < WZ; z++)
					s_saveBuf.world_data[idx++] = world[x][y][z];
	}

	s_saveBuf.px      = px;
	s_saveBuf.py      = py;
	s_saveBuf.pz      = pz;
	s_saveBuf.vy      = vy;
	s_saveBuf.yaw     = yaw;
	s_saveBuf.pitch   = pitch;
	s_saveBuf.flying  = flying;
	s_saveBuf.selected = selected;
	{
		int i;
		for (i = 0; i < NUM_BLOCK_TYPES; i++)
			s_saveBuf.inventory[i] = inventory[i];
	}
	s_saveBuf.timeOfDay     = timeOfDay;
	s_saveBuf.inventorySword = inventorySword;

	FILE *f = fopen(SAVE_PATH, "wb");
	if (!f) return 0;

	int ok = (fwrite(&s_saveBuf, sizeof(s_saveBuf), 1, f) == 1);
	fclose(f);
	return ok;
}

/* Returns 1 if a valid save was found and loaded, 0 otherwise. */
static int loadWorld(void)
{
	if (!s_fatOk) return 0;

	FILE *f = fopen(SAVE_PATH, "rb");
	if (!f) return 0;

	int ok = (fread(&s_saveBuf, sizeof(s_saveBuf), 1, f) == 1);
	fclose(f);

	if (!ok || s_saveBuf.magic != SAVE_MAGIC || s_saveBuf.version != SAVE_VER)
		return 0;

	{
		int x, y, z, idx = 0;
		for (x = 0; x < WX; x++)
			for (y = 0; y < WY; y++)
				for (z = 0; z < WZ; z++)
					world[x][y][z] = s_saveBuf.world_data[idx++];
	}

	px      = s_saveBuf.px;
	py      = s_saveBuf.py;
	pz      = s_saveBuf.pz;
	vy      = s_saveBuf.vy;
	yaw     = s_saveBuf.yaw;
	pitch   = s_saveBuf.pitch;
	flying  = s_saveBuf.flying;
	onGround = 0;
	selected = s_saveBuf.selected;
	{
		int i;
		for (i = 0; i < NUM_BLOCK_TYPES; i++)
			inventory[i] = s_saveBuf.inventory[i];
	}
	timeOfDay     = s_saveBuf.timeOfDay;
	inventorySword = s_saveBuf.inventorySword;
	swingCooldown  = 0;

	return 1;
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
	videoSetMode(MODE_0_3D);
	consoleDemoInit();

	glInit();
	glEnable(GL_ANTIALIAS);

	glClearColor(10, 18, 26, 31);
	glClearPolyID(63);
	glClearDepth(0x7FFF);
	glViewport(0, 0, 255, 191);

	initFAT();

	if (!loadWorld()) {
		/* no save found (or no FAT) — generate a fresh world */
		generateWorld();
		resetPlayer();
	}
	playerHP         = PLAYER_MAX_HP;
	playerInvincible = 0;
	knockVX = 0.0f;
	knockVZ = 0.0f;
	spawnAnimals();
	initMobs();

	int breakCooldown = 0;
	int prevTouchValid = 0;
	touchPosition prevTouch = { 0 };

	iprintf("\x1b[2J");

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

		if (pitch >  1.45f) pitch =  1.45f;
		if (pitch < -1.45f) pitch = -1.45f;

		/* ---- movement ---- */
		{
			float speed = flying ? FLY_SPEED : WALK_SPEED;
			float fx = sinf(yaw), fz = -cosf(yaw);
			float dx = 0.0f, dz = 0.0f, dy = 0.0f;

			if (held & KEY_UP)    { dx += fx * speed; dz += fz * speed; }
			if (held & KEY_DOWN)  { dx -= fx * speed; dz -= fz * speed; }
			if (held & KEY_LEFT)  { dx += fz * speed; dz -= fx * speed; }
			if (held & KEY_RIGHT) { dx -= fz * speed; dz += fx * speed; }

			if (flying) {
				vy = 0.0f;
				if (held & KEY_A) dy += speed;
				if (held & KEY_B) dy -= speed;
			} else {
				if ((down & KEY_A) && onGround) {
					vy = JUMP_V;
					onGround = 0;
				}
				vy -= GRAVITY;
				if (vy < -0.4f) vy = -0.4f;
				dy = vy;
			}

			movePlayer(dx, dy, dz);
		}

		if (down & KEY_X) { flying = !flying; vy = 0.0f; }
		if (down & KEY_Y) { hudPage = !hudPage; iprintf("\x1b[2J"); }

		/* ---- swing cooldown tick ---- */
		if (swingCooldown > 0) swingCooldown--;

		/* ---- sword crafting: L+B (walk mode) ---- */
		if (!flying && (held & KEY_L) && (down & KEY_B)) {
			if (inventory[BLOCK_PLANKS] >= SWORD_CRAFT_PLANKS) {
				inventory[BLOCK_PLANKS] -= SWORD_CRAFT_PLANKS;
				inventorySword++;
				iprintf("\x1b[22;0H  [Sword crafted!]            ");
			} else {
				iprintf("\x1b[22;0H  [Need %d Planks to craft!]  ",
				        SWORD_CRAFT_PLANKS);
			}
		/* ---- sword swing: B alone (walk mode, has sword) ---- */
		} else if (!flying && (down & KEY_B)) {
			if (inventorySword > 0 && swingCooldown == 0) {
				int i, anyHit = 0;
				float eyeY = py + EYE_HEIGHT * 0.5f;
				swingCooldown = SWING_COOLDOWN;
				for (i = 0; i < NUM_MOBS; i++) {
					HostileMob *m = &mobs[i];
					if (!m->alive || m->flashTimer > 0) continue;
					float mdx = m->x - px, mdz = m->z - pz;
					float hdist = sqrtf(mdx * mdx + mdz * mdz);
					float vdist = fabsf((m->y + 0.65f) - eyeY);
					if (hdist < SWORD_REACH && vdist < SWORD_VERT_REACH) {
						m->flashTimer = MOB_FLASH_FRAMES;
						anyHit = 1;
					}
				}
				if (anyHit)
					iprintf("\x1b[22;0H  [Hit!]                      ");
			} else if (inventorySword == 0) {
				iprintf("\x1b[22;0H  [No sword - craft with L+B] ");
			}
		}

		/* ---- block interaction ---- */
		RayHit look = raycast(5.0f);

		if (down & KEY_SELECT) {
			int i;
			for (i = 1; i < NUM_BLOCK_TYPES; i++) {
				selected++;
				if (selected >= NUM_BLOCK_TYPES) selected = 1;
				if (selected != BLOCK_WATER) break;
			}
		}

		/* break: hold L (small cooldown so it feels like digging) */
		if (breakCooldown > 0) breakCooldown--;
		if ((held & KEY_L) && look.hit && breakCooldown == 0) {
			u8 b = world[look.bx][look.by][look.bz];
			if (isCollectable(b)) {
				if (inventory[b] < 99) inventory[b]++;
				/* dug-out spots below sea level fill with water */
				if (look.by <= WATER_LEVEL)
					world[look.bx][look.by][look.bz] = BLOCK_WATER;
				else
					world[look.bx][look.by][look.bz] = BLOCK_AIR;
				breakCooldown = 12;
			}
		}

		/* place: R, consumes inventory */
		if ((down & KEY_R) && look.hit &&
		    inWorld(look.nx, look.ny, look.nz) &&
		    inventory[selected] > 0) {
			u8 t = getBlock(look.nx, look.ny, look.nz);
			if (t == BLOCK_AIR || t == BLOCK_WATER || t == BLOCK_FLOWER) {
				/* don't place inside the player */
				world[look.nx][look.ny][look.nz] = selected;
				if (boxCollides(px, py, pz)) {
					world[look.nx][look.ny][look.nz] = t;   /* undo */
				} else {
					inventory[selected]--;
				}
			}
		}

		/* L+R+START = save world */
		if ((held & KEY_L) && (held & KEY_R) && (down & KEY_START)) {
			if (saveWorld()) {
				iprintf("\x1b[22;0H  [World saved!]              ");
			} else if (!s_fatOk) {
				iprintf("\x1b[22;0H  [No SD - cannot save]       ");
			} else {
				iprintf("\x1b[22;0H  [Save failed!]              ");
			}
		} else if (down & KEY_START) {
			/* START alone = new world */
			worldSeed = worldSeed * 1103515245u + 12345u;
			generateWorld();
			resetPlayer();
			playerHP         = PLAYER_MAX_HP;
			playerInvincible = 0;
			knockVX = 0.0f;
			knockVZ = 0.0f;
			spawnAnimals();
			initMobs();
			iprintf("\x1b[2J");
		}

		/* ---- knockback decay ---- */
		if (knockVX != 0.0f || knockVZ != 0.0f) {
			movePlayer(knockVX, 0.0f, knockVZ);
			knockVX *= 0.6f;
			knockVZ *= 0.6f;
			if (knockVX * knockVX + knockVZ * knockVZ < 0.0001f) {
				knockVX = 0.0f;
				knockVZ = 0.0f;
			}
		}

		/* ---- invincibility tick ---- */
		if (playerInvincible > 0) playerInvincible--;

		/* ---- simulation ---- */
		updateAnimals();
		updateMobs();
		updateDayNight();

		/* ---- HUD ---- */
		drawHUD(&look);

		/* ---- render ---- */
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		gluPerspective(70, 256.0f / 192.0f, 0.1f, 40.0f);

		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		{
			float ey = py + EYE_HEIGHT;
			float lx = px + cosf(pitch) * sinf(yaw);
			float ly = ey + sinf(pitch);
			float lz = pz - cosf(pitch) * cosf(yaw);
			gluLookAt(px, ey, pz, lx, ly, lz, 0.0f, 1.0f, 0.0f);
		}

		glPolyFmt(POLY_ALPHA(31) | POLY_CULL_BACK);

		drawWorld(&look);
		drawAnimals();
		drawMobs();

		glFlush(0);
		swiWaitForVBlank();
	}

	return 0;
}
