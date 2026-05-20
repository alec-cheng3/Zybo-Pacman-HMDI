#include "pacman_game.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "xil_printf.h"
#include "xtime_l.h"
#include "sleep.h"

#include "renderer.h"
#include "profiler.h"

// ============================================================================
// SCREEN CONSTANTS
// ============================================================================
#define SCREEN_WIDTH   1920
#define SCREEN_HEIGHT  1080

#define FPS            60
#define FRAME_DELAY_US (1000000 / FPS)

// ============================================================================
// MAZE / TILE CONSTANTS
// ============================================================================
#define TILE_SIZE      36
#define MAZE_ROWS      21
#define MAZE_COLS      28

#define MAZE_PIXEL_W   (MAZE_COLS * TILE_SIZE)
#define MAZE_PIXEL_H   (MAZE_ROWS * TILE_SIZE)

#define MAZE_OFFSET_X  ((SCREEN_WIDTH  - MAZE_PIXEL_W) / 2)
#define MAZE_OFFSET_Y  (((SCREEN_HEIGHT - MAZE_PIXEL_H) / 2) + 40)

// ============================================================================
// GAME CONSTANTS
// ============================================================================
#define PACMAN_RADIUS  15
#define PACMAN_SPEED   9

#define PELLET_RADIUS  4
#define POWER_PELLET_RADIUS 8

#define STARTING_LIVES 3

#define FONT_SCALE 5

#define LEADERBOARD_COUNT 10
#define LEADERBOARD_NAME_LEN 4

#define NAME_ENTRY_LENGTH 3

#define BTN_NEXT_LETTER 0x1
#define BTN_PREV_LETTER 0x2
#define BTN_NEXT_CHAR   0x4

#define GHOST_COUNT 4
#define GHOST_RADIUS 14

#define GHOST_SPEED             6
#define GHOST_FRIGHTENED_SPEED  3

#define CHASE_DURATION_FRAMES      (FPS * 10)
#define SCATTER_DURATION_FRAMES    (FPS * 2)
#define FRIGHTENED_DURATION_FRAMES (FPS * 2)

#define GHOST_BLINKY 0
#define GHOST_PINKY  1
#define GHOST_INKY   2
#define GHOST_CLYDE  3

// ============================================================================
// COLORS
// ============================================================================
#define BLACK_R   0
#define BLACK_G   0
#define BLACK_B   0

#define BLUE_R    0
#define BLUE_G    0
#define BLUE_B    255

#define YELLOW_R  255
#define YELLOW_G  255
#define YELLOW_B  0

#define WHITE_R   255
#define WHITE_G   255
#define WHITE_B   255

#define RED_R     255
#define RED_G     0
#define RED_B     0

#define PINK_R    255
#define PINK_G    105
#define PINK_B    180

#define CYAN_R    0
#define CYAN_G    255
#define CYAN_B    255

#define ORANGE_R  255
#define ORANGE_G  165
#define ORANGE_B  0

#define FRIGHT_R  0
#define FRIGHT_G  0
#define FRIGHT_B  180

// ============================================================================
// TYPES
// ============================================================================
typedef enum {
    DIR_NONE = 0,
    DIR_LEFT,
    DIR_RIGHT,
    DIR_UP,
    DIR_DOWN
} Direction;

typedef enum {
    GHOST_MODE_CHASE = 0,
    GHOST_MODE_SCATTER,
    GHOST_MODE_FRIGHTENED
} GhostMode;

typedef struct {
    int x;
    int y;

    Direction dir;
    Direction next_dir;

    int mouth_counter;
    int mouth_phase;
} Pacman;

typedef struct {
    int x;
    int y;

    Direction dir;

    u8 r;
    u8 g;
    u8 b;

    int scatter_row;
    int scatter_col;

    int home_row;
    int home_col;

    int slow_until_center;
} Ghost;

typedef struct {
    Pacman pacman;

    Ghost ghosts[GHOST_COUNT];
    int pellets[MAZE_ROWS][MAZE_COLS];
    int pellets_remaining;

    int score;
    int lives;

    GhostMode ghost_mode;
    int ghost_mode_timer;
    int frightened_timer;

    unsigned int ghost_random_seed;

    int game_running;
    int game_over;
    int game_won;
    int entering_name;
    int name_entry_done;
    int selected_name_char;
    char current_name[LEADERBOARD_NAME_LEN];
    int leaderboard_entry_index;
    int game_over_frame_counter;
} GameState;

typedef struct {
    char name[LEADERBOARD_NAME_LEN];
    int score;
} LeaderboardEntry;

// ============================================================================
// GLOBALS
// ============================================================================
static profiler_s profiler_pacman[10];

/*
 * # = wall
 * space = path
 *
 * 21 rows x 28 columns
 *
 * Row 10 has left/right tunnel openings.
 */
static char maze[MAZE_ROWS][MAZE_COLS + 1] = {
    "############################",
    "#            ##            #",
    "# #### ##### ## ##### #### #",
    "# #### ##### ## ##### #### #",
    "#                          #",
    "# #### ## ######## ## #### #",
    "#      ##    ##    ##      #",
    "###### ##### ## ##### ######",
    "###### #            # ######",
    "###### # ###  ### # # ######",
    "       # #      # # #       ",
    "###### # ###  ### # # ######",
    "###### #            # ######",
    "###### ##### ## ##### ######",
    "#      ##    ##    ##      #",
    "# #### ## ######## ## #### #",
    "#                          #",
    "# #### ##### ## ##### #### #",
    "# #### ##### ## ##### #### #",
    "#            ##            #",
    "############################"
};

static LeaderboardEntry leaderboard[LEADERBOARD_COUNT] = {
    {"AAA", 0},
    {"BBB", 0},
    {"CCC", 0},
    {"DDD", 0},
    {"EEE", 0},
    {"FFF", 0},
    {"GGG", 0},
    {"HHH", 0},
    {"III", 0},
    {"JJJ", 0}
};

// ============================================================================
// TILE HELPERS
// ============================================================================
static int col_to_pixel_center(int col)
{
    return MAZE_OFFSET_X + col * TILE_SIZE + TILE_SIZE / 2;
}

static int row_to_pixel_center(int row)
{
    return MAZE_OFFSET_Y + row * TILE_SIZE + TILE_SIZE / 2;
}

static int pixel_to_col(int x)
{
    return (x - MAZE_OFFSET_X) / TILE_SIZE;
}

static int pixel_to_row(int y)
{
    return (y - MAZE_OFFSET_Y) / TILE_SIZE;
}

static int is_exact_tile_center(int x, int y)
{
    int col = pixel_to_col(x);
    int row = pixel_to_row(y);

    if (row < 0 || row >= MAZE_ROWS ||
        col < 0 || col >= MAZE_COLS) {
        return 0;
    }

    int cx = col_to_pixel_center(col);
    int cy = row_to_pixel_center(row);

    return x == cx && y == cy;
}

static int is_wall_tile(int row, int col)
{
    /*
     * Allow leaving the maze only through the tunnel row.
     */
    if (row == 10 && (col < 0 || col >= MAZE_COLS)) {
        return 0;
    }

    if (row < 0 || row >= MAZE_ROWS || col < 0 || col >= MAZE_COLS) {
        return 1;
    }

    return maze[row][col] == '#';
}

static int can_move_from_tile(int row, int col, Direction dir)
{
    int next_row = row;
    int next_col = col;

    switch (dir) {
        case DIR_LEFT:
            next_col--;
            break;

        case DIR_RIGHT:
            next_col++;
            break;

        case DIR_UP:
            next_row--;
            break;

        case DIR_DOWN:
            next_row++;
            break;

        default:
            return 0;
    }

    return !is_wall_tile(next_row, next_col);
}

static void move_entity(int *x, int *y, Direction dir, int speed)
{
    switch (dir) {
        case DIR_LEFT:
            *x -= speed;
            break;

        case DIR_RIGHT:
            *x += speed;
            break;

        case DIR_UP:
            *y -= speed;
            break;

        case DIR_DOWN:
            *y += speed;
            break;

        default:
            break;
    }
}

static int direction_dx(Direction dir)
{
    switch (dir) {
        case DIR_LEFT:
            return -1;

        case DIR_RIGHT:
            return 1;

        default:
            return 0;
    }
}

static int direction_dy(Direction dir)
{
    switch (dir) {
        case DIR_UP:
            return -1;

        case DIR_DOWN:
            return 1;

        default:
            return 0;
    }
}

static Direction opposite_dir(Direction dir)
{
    switch (dir) {
        case DIR_LEFT:
            return DIR_RIGHT;

        case DIR_RIGHT:
            return DIR_LEFT;

        case DIR_UP:
            return DIR_DOWN;

        case DIR_DOWN:
            return DIR_UP;

        default:
            return DIR_NONE;
    }
}

static int tile_distance_sq(int row_a, int col_a, int row_b, int col_b)
{
    int dr = row_a - row_b;
    int dc = col_a - col_b;

    return dr * dr + dc * dc;
}

static unsigned int ghost_random(GameState *game)
{
    game->ghost_random_seed =
        game->ghost_random_seed * 1103515245u + 12345u;

    return game->ghost_random_seed;
}

static void wrap_tunnel_entity(int *x, int y)
{
    /*
     * Keep entities aligned after using the tunnel.
     */
    if (y == row_to_pixel_center(10)) {
        if (*x < MAZE_OFFSET_X) {
            *x = col_to_pixel_center(MAZE_COLS - 1);
        } else if (*x > MAZE_OFFSET_X + MAZE_PIXEL_W) {
            *x = col_to_pixel_center(0);
        }
    }
}

// ============================================================================
// DRAWING
// ============================================================================
static void draw_rect(int x, int y, int w, int h, u8 r, u8 g, u8 b)
{
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            if (px >= 0 && px < SCREEN_WIDTH &&
                py >= 0 && py < SCREEN_HEIGHT) {
                renderer_draw_pixel(px, py, r, g, b);
            }
        }
    }
}

static void draw_filled_circle(int cx, int cy, int radius, u8 r, u8 g, u8 b)
{
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            if (x * x + y * y <= radius * radius) {
                int px = cx + x;
                int py = cy + y;

                if (px >= 0 && px < SCREEN_WIDTH &&
                    py >= 0 && py < SCREEN_HEIGHT) {
                    renderer_draw_pixel(px, py, r, g, b);
                }
            }
        }
    }
}


static const unsigned char digit_font[10][7] = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, // 0
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, // 1
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, // 2
    {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}, // 3
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, // 4
    {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}, // 5
    {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E}, // 6
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, // 7
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, // 8
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E}  // 9
};

static const unsigned char letter_S[7] = {
    0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E
};

static const unsigned char letter_C[7] = {
    0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E
};

static const unsigned char letter_O[7] = {
    0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E
};

static const unsigned char letter_R[7] = {
    0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11
};

static const unsigned char letter_E[7] = {
    0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F
};

static const unsigned char letter_L[7] = {
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F
};

static const unsigned char letter_I[7] = {
    0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E
};

static const unsigned char letter_F[7] = {
    0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10
};

static const unsigned char letter_V[7] = {
    0x11, 0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04
};

static const unsigned char letter_G[7] = {
    0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E
};

static const unsigned char letter_A[7] = {
    0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11
};

static const unsigned char letter_M[7] = {
    0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11
};

static const unsigned char letter_Y[7] = {
    0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04
};

static const unsigned char letter_U[7] = {
    0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E
};

static const unsigned char letter_W[7] = {
    0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11
};

static const unsigned char letter_N[7] = {
    0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11
};

static const unsigned char letter_B[7] = {
    0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E
};

static const unsigned char letter_D[7] = {
    0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E
};

static const unsigned char letter_H[7] = {
    0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11
};

static const unsigned char letter_J[7] = {
    0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E
};

static const unsigned char letter_K[7] = {
    0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11
};

static const unsigned char letter_P[7] = {
    0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10
};

static const unsigned char letter_Q[7] = {
    0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D
};

static const unsigned char letter_T[7] = {
    0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04
};

static const unsigned char letter_X[7] = {
    0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11
};

static const unsigned char letter_Z[7] = {
    0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F
};

static void draw_bitmap_char(int x, int y, const unsigned char bitmap[7],
                             u8 r, u8 g, u8 b)
{
    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 5; col++) {
            if (bitmap[row] & (1 << (4 - col))) {
                draw_rect(x + col * FONT_SCALE,
                          y + row * FONT_SCALE,
                          FONT_SCALE,
                          FONT_SCALE,
                          r, g, b);
            }
        }
    }
}

static void draw_char(int x, int y, char c, u8 r, u8 g, u8 b)
{
    if (c >= '0' && c <= '9') {
        draw_bitmap_char(x, y, digit_font[c - '0'], r, g, b);
    } else if (c == 'S') {
        draw_bitmap_char(x, y, letter_S, r, g, b);
    } else if (c == 'C') {
        draw_bitmap_char(x, y, letter_C, r, g, b);
    } else if (c == 'O') {
        draw_bitmap_char(x, y, letter_O, r, g, b);
    } else if (c == 'R') {
        draw_bitmap_char(x, y, letter_R, r, g, b);
    } else if (c == 'E') {
        draw_bitmap_char(x, y, letter_E, r, g, b);
    } else if (c == 'L') {
        draw_bitmap_char(x, y, letter_L, r, g, b);
    } else if (c == 'I') {
        draw_bitmap_char(x, y, letter_I, r, g, b);
    } else if (c == 'F') {
        draw_bitmap_char(x, y, letter_F, r, g, b);
    } else if (c == 'V') {
        draw_bitmap_char(x, y, letter_V, r, g, b);
    } else if (c == 'G') {
        draw_bitmap_char(x, y, letter_G, r, g, b);
    } else if (c == 'A') {
        draw_bitmap_char(x, y, letter_A, r, g, b);
    } else if (c == 'M') {
        draw_bitmap_char(x, y, letter_M, r, g, b);
    } else if (c == 'Y') {
        draw_bitmap_char(x, y, letter_Y, r, g, b);
    } else if (c == 'U') {
        draw_bitmap_char(x, y, letter_U, r, g, b);
    } else if (c == 'W') {
        draw_bitmap_char(x, y, letter_W, r, g, b);
    } else if (c == 'N') {
        draw_bitmap_char(x, y, letter_N, r, g, b);
    } else if (c == 'B') {
        draw_bitmap_char(x, y, letter_B, r, g, b);
    } else if (c == 'D') {
        draw_bitmap_char(x, y, letter_D, r, g, b);
    } else if (c == 'H') {
        draw_bitmap_char(x, y, letter_H, r, g, b);
    } else if (c == 'J') {
        draw_bitmap_char(x, y, letter_J, r, g, b);
    } else if (c == 'K') {
        draw_bitmap_char(x, y, letter_K, r, g, b);
    } else if (c == 'P') {
        draw_bitmap_char(x, y, letter_P, r, g, b);
    } else if (c == 'Q') {
        draw_bitmap_char(x, y, letter_Q, r, g, b);
    } else if (c == 'T') {
        draw_bitmap_char(x, y, letter_T, r, g, b);
    } else if (c == 'X') {
        draw_bitmap_char(x, y, letter_X, r, g, b);
    } else if (c == 'Z') {
        draw_bitmap_char(x, y, letter_Z, r, g, b);
    }
}

static void draw_text(int x, int y, const char *text, u8 r, u8 g, u8 b)
{
    int cursor_x = x;

    while (*text) {
        if (*text != ' ') {
            draw_char(cursor_x, y, *text, r, g, b);
        }

        cursor_x += 6 * FONT_SCALE;
        text++;
    }
}

static void draw_leaderboard(GameState *game)
{
    int x = MAZE_OFFSET_X + MAZE_PIXEL_W + 40;
    int y = 20;
    int entry_spacing = 45;

    char line[32];

    draw_text(x,
              y,
              "LEADERBOARD",
              YELLOW_R,
              YELLOW_G,
              YELLOW_B);

    y += 60;

    draw_text(x,
              y,
              "R NAME SCORE",
              YELLOW_R,
              YELLOW_G,
              YELLOW_B);

    y += 50;

    for (int i = 0; i < LEADERBOARD_COUNT; i++) {
        int row_y = y + i * entry_spacing;
        sprintf(line, "%d", i + 1);
        draw_text(x,
                  row_y,
                  line,
                  WHITE_R,
                  WHITE_G,
                  WHITE_B);

        int name_x = x + 70;

        if (game->game_over &&
            game->entering_name &&
            i == game->leaderboard_entry_index) {

            for (int c = 0; c < NAME_ENTRY_LENGTH; c++) {
                int show_char = 1;

                /*
                 * Flash selected character.
                 */
                if (c == game->selected_name_char) {
                    show_char = ((game->game_over_frame_counter / 8) % 2) == 0;
                }

                if (show_char) {
                    char one_char[2];
                    one_char[0] = leaderboard[i].name[c];
                    one_char[1] = '\0';

                    draw_text(name_x + c * 6 * FONT_SCALE,
                              row_y,
                              one_char,
                              YELLOW_R,
                              YELLOW_G,
                              YELLOW_B);
                }
            }
        } else {
            draw_text(name_x,
                      row_y,
                      leaderboard[i].name,
                      WHITE_R,
                      WHITE_G,
                      WHITE_B);
        }

        sprintf(line, "%d", leaderboard[i].score);
        draw_text(x + 180,
                  row_y,
                  line,
                  WHITE_R,
                  WHITE_G,
                  WHITE_B);
    }
}

static void update_leaderboard_name(GameState *game)
{
    int index = game->leaderboard_entry_index;

    if (index < 0 || index >= LEADERBOARD_COUNT) {
        return;
    }

    leaderboard[index].name[0] = game->current_name[0];
    leaderboard[index].name[1] = game->current_name[1];
    leaderboard[index].name[2] = game->current_name[2];
    leaderboard[index].name[3] = '\0';
}

static void draw_end_message(GameState *game)
{
    const char *text;

    if (game->game_won) {
        text = "YOU WON";
    } else {
        text = "GAME OVER";
    }

    int text_width = 0;
    const char *p = text;

    while (*p) {
        text_width += 6 * FONT_SCALE;
        p++;
    }

    int x = (SCREEN_WIDTH - text_width) / 2;
    int y = 20;

    if (game->game_won) {
        draw_text(x,
                  y,
                  text,
                  YELLOW_R,
                  YELLOW_G,
                  YELLOW_B);
    } else {
        draw_text(x,
                  y,
                  text,
                  RED_R,
                  RED_G,
                  RED_B);
    }
}

static void draw_score(GameState *game)
{
    char text[32];

    sprintf(text, "SCORE %d", game->score);

    draw_text(MAZE_OFFSET_X,
              20,
              text,
              WHITE_R,
              WHITE_G,
              WHITE_B);
}

static int point_in_mouth_cutout(int dx, int dy, Direction dir, int mouth_phase)
{
    if (dir == DIR_NONE) {
        dir = DIR_RIGHT;
    }

    int openness;

    switch (mouth_phase) {
        case 0:
            openness = 0;
            break;

        case 1:
        case 5:
            openness = 3;
            break;

        case 2:
        case 4:
            openness = 6;
            break;

        case 3:
        default:
            openness = 9;
            break;
    }

    if (openness == 0) {
        return 0;
    }

    switch (dir) {
        case DIR_RIGHT:
            return dx > 0 && abs(dy) < (dx * openness) / 10;

        case DIR_LEFT:
            return dx < 0 && abs(dy) < (-dx * openness) / 10;

        case DIR_UP:
            return dy < 0 && abs(dx) < (-dy * openness) / 10;

        case DIR_DOWN:
            return dy > 0 && abs(dx) < (dy * openness) / 10;

        default:
            return 0;
    }
}

static void draw_pacman(int cx, int cy, int radius, Direction dir, int mouth_phase)
{
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            if (x * x + y * y <= radius * radius) {
                int px = cx + x;
                int py = cy + y;

                if (px < 0 || px >= SCREEN_WIDTH ||
                    py < 0 || py >= SCREEN_HEIGHT) {
                    continue;
                }

                if (point_in_mouth_cutout(x, y, dir, mouth_phase)) {
                    renderer_draw_pixel(px, py, BLACK_R, BLACK_G, BLACK_B);
                } else {
                    renderer_draw_pixel(px, py, YELLOW_R, YELLOW_G, YELLOW_B);
                }
            }
        }
    }
}

static void draw_life_icon(int cx, int cy)
{
    /*
     * Small Pac-Man life icon facing right.
     */
    int radius = 12;

    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            if (x * x + y * y <= radius * radius) {
                int px = cx + x;
                int py = cy + y;

                if (px < 0 || px >= SCREEN_WIDTH ||
                    py < 0 || py >= SCREEN_HEIGHT) {
                    continue;
                }

                if (point_in_mouth_cutout(x, y, DIR_RIGHT, 3)) {
                    renderer_draw_pixel(px, py, BLACK_R, BLACK_G, BLACK_B);
                } else {
                    renderer_draw_pixel(px, py, YELLOW_R, YELLOW_G, YELLOW_B);
                }
            }
        }
    }
}

static void draw_lives(GameState *game)
{
    int x = MAZE_OFFSET_X;
    int y = 90;

    draw_text(x,
              y,
              "LIVES",
              WHITE_R,
              WHITE_G,
              WHITE_B);

    /*
     * Icons moved farther right from the word LIVES.
     */
    int icon_start_x = x + 170;
    int icon_y = y + 14;

    for (int i = 0; i < game->lives; i++) {
        draw_life_icon(icon_start_x + i * 42,
                       icon_y);
    }
}

static void draw_ghost(int cx, int cy, u8 r, u8 g, u8 b)
{
    draw_filled_circle(cx, cy - 5, GHOST_RADIUS, r, g, b);

    draw_rect(cx - GHOST_RADIUS,
              cy - 5,
              GHOST_RADIUS * 2 + 1,
              GHOST_RADIUS + 6,
              r, g, b);

    int bottom_y = cy + GHOST_RADIUS + 1;
    int left_x = cx - GHOST_RADIUS;
    int right_x = cx + GHOST_RADIUS;

    for (int py = cy + 4; py <= bottom_y; py++) {
        for (int px = left_x; px <= right_x; px++) {
            int local_x = px - left_x;

            int wave = local_x % 14;
            int extra_drop;

            if (wave < 7) {
                extra_drop = wave;
            } else {
                extra_drop = 14 - wave;
            }

            extra_drop = extra_drop * 2;

            int edge_y = bottom_y - extra_drop;

            if (py <= edge_y) {
                if (px >= 0 && px < SCREEN_WIDTH &&
                    py >= 0 && py < SCREEN_HEIGHT) {
                    renderer_draw_pixel(px, py, r, g, b);
                }
            }
        }
    }

    draw_filled_circle(cx - 5, cy - 7, 3, WHITE_R, WHITE_G, WHITE_B);
    draw_filled_circle(cx + 5, cy - 7, 3, WHITE_R, WHITE_G, WHITE_B);
}

// ============================================================================
// GHOST INIT / RESET
// ============================================================================
static void init_ghosts(GameState *game)
{
    /*
     * Ghost starting positions are in the central open tunnel row.
     */
    game->ghosts[GHOST_BLINKY].home_row = 10;
    game->ghosts[GHOST_BLINKY].home_col = 12;
    game->ghosts[GHOST_BLINKY].x = col_to_pixel_center(12);
    game->ghosts[GHOST_BLINKY].y = row_to_pixel_center(10);
    game->ghosts[GHOST_BLINKY].dir = DIR_LEFT;
    game->ghosts[GHOST_BLINKY].r = RED_R;
    game->ghosts[GHOST_BLINKY].g = RED_G;
    game->ghosts[GHOST_BLINKY].b = RED_B;
    game->ghosts[GHOST_BLINKY].scatter_row = 1;
    game->ghosts[GHOST_BLINKY].scatter_col = 26;

    game->ghosts[GHOST_PINKY].home_row = 10;
    game->ghosts[GHOST_PINKY].home_col = 13;
    game->ghosts[GHOST_PINKY].x = col_to_pixel_center(13);
    game->ghosts[GHOST_PINKY].y = row_to_pixel_center(10);
    game->ghosts[GHOST_PINKY].dir = DIR_RIGHT;
    game->ghosts[GHOST_PINKY].r = PINK_R;
    game->ghosts[GHOST_PINKY].g = PINK_G;
    game->ghosts[GHOST_PINKY].b = PINK_B;
    game->ghosts[GHOST_PINKY].scatter_row = 1;
    game->ghosts[GHOST_PINKY].scatter_col = 1;

    game->ghosts[GHOST_INKY].home_row = 10;
    game->ghosts[GHOST_INKY].home_col = 14;
    game->ghosts[GHOST_INKY].x = col_to_pixel_center(14);
    game->ghosts[GHOST_INKY].y = row_to_pixel_center(10);
    game->ghosts[GHOST_INKY].dir = DIR_LEFT;
    game->ghosts[GHOST_INKY].r = CYAN_R;
    game->ghosts[GHOST_INKY].g = CYAN_G;
    game->ghosts[GHOST_INKY].b = CYAN_B;
    game->ghosts[GHOST_INKY].scatter_row = 19;
    game->ghosts[GHOST_INKY].scatter_col = 26;

    game->ghosts[GHOST_CLYDE].home_row = 10;
    game->ghosts[GHOST_CLYDE].home_col = 15;
    game->ghosts[GHOST_CLYDE].x = col_to_pixel_center(15);
    game->ghosts[GHOST_CLYDE].y = row_to_pixel_center(10);
    game->ghosts[GHOST_CLYDE].dir = DIR_RIGHT;
    game->ghosts[GHOST_CLYDE].r = ORANGE_R;
    game->ghosts[GHOST_CLYDE].g = ORANGE_G;
    game->ghosts[GHOST_CLYDE].b = ORANGE_B;
    game->ghosts[GHOST_CLYDE].scatter_row = 19;
    game->ghosts[GHOST_CLYDE].scatter_col = 1;

    game->ghosts[GHOST_BLINKY].slow_until_center = 0;
    game->ghosts[GHOST_PINKY].slow_until_center = 0;
    game->ghosts[GHOST_INKY].slow_until_center = 0;
    game->ghosts[GHOST_CLYDE].slow_until_center = 0;
}

static void reset_ghost_to_home(GameState *game, int ghost_index)
{
    Ghost *ghost = &game->ghosts[ghost_index];

    ghost->x = col_to_pixel_center(ghost->home_col);
    ghost->y = row_to_pixel_center(ghost->home_row);



    int row = ghost->home_row;
    int col = ghost->home_col;

    if (can_move_from_tile(row, col, DIR_LEFT)) {
        ghost->dir = DIR_LEFT;
    } else if (can_move_from_tile(row, col, DIR_RIGHT)) {
        ghost->dir = DIR_RIGHT;
    } else if (can_move_from_tile(row, col, DIR_UP)) {
        ghost->dir = DIR_UP;
    } else if (can_move_from_tile(row, col, DIR_DOWN)) {
        ghost->dir = DIR_DOWN;
    } else {
        ghost->dir = DIR_NONE;
    }
    ghost->slow_until_center = 0;
}

static void respawn_after_death(GameState *game)
{
    /*
     * Reset Pac-Man to starting position.
     */
    game->pacman.x = col_to_pixel_center(1);
    game->pacman.y = row_to_pixel_center(10);

    game->pacman.dir = DIR_NONE;
    game->pacman.next_dir = DIR_NONE;

    game->pacman.mouth_counter = 0;
    game->pacman.mouth_phase = 0;

    /*
     * Reset all ghosts to home.
     */
    for (int i = 0; i < GHOST_COUNT; i++) {
        reset_ghost_to_home(game, i);
    }

    game->ghost_mode = GHOST_MODE_SCATTER;
    game->ghost_mode_timer = 0;
    game->frightened_timer = 0;
}

static int add_score_to_leaderboard(const char *name, int score)
{
    LeaderboardEntry new_entry;

    new_entry.name[0] = name[0] ? name[0] : 'A';
    new_entry.name[1] = name[1] ? name[1] : 'A';
    new_entry.name[2] = name[2] ? name[2] : 'A';
    new_entry.name[3] = '\0';
    new_entry.score = score;

    for (int i = 0; i < LEADERBOARD_COUNT; i++) {
        if (score > leaderboard[i].score) {
            for (int j = LEADERBOARD_COUNT - 1; j > i; j--) {
                leaderboard[j] = leaderboard[j - 1];
            }

            leaderboard[i] = new_entry;
            return i;
        }
    }

    return -1;
}

static void enter_game_over_state(GameState *game)
{
    if (game->game_over) {
        return;
    }

    game->game_over = 1;

    game->entering_name = 1;
    game->name_entry_done = 0;
    game->selected_name_char = 0;
    game->game_over_frame_counter = 0;

    game->current_name[0] = 'A';
    game->current_name[1] = 'A';
    game->current_name[2] = 'A';
    game->current_name[3] = '\0';

    game->leaderboard_entry_index =
        add_score_to_leaderboard(game->current_name, game->score);

    /*
     * If the score did not make the top 10, skip name entry.
     */
    if (game->leaderboard_entry_index < 0) {
        game->entering_name = 0;
        game->name_entry_done = 1;
    }

    game->pacman.dir = DIR_NONE;
    game->pacman.next_dir = DIR_NONE;

    for (int g = 0; g < GHOST_COUNT; g++) {
        game->ghosts[g].dir = DIR_NONE;
        game->ghosts[g].slow_until_center = 0;
    }

    game->frightened_timer = 0;
}
// ============================================================================
// PELLETS
// ============================================================================
static void init_pellets(GameState *game)
{
    game->pellets_remaining = 0;

    for (int row = 0; row < MAZE_ROWS; row++) {
        for (int col = 0; col < MAZE_COLS; col++) {
            /*
             * Put pellets on open path tiles.
             * Skip the tunnel row so pellets do not appear in the side tunnel.
             */
            if (maze[row][col] == ' ' && row != 10) {
                game->pellets[row][col] = 1;
                game->pellets_remaining++;
            } else {
                game->pellets[row][col] = 0;
            }
        }
    }

    /*
     * Power pellets.
     */
    if (game->pellets[1][1]) {
        game->pellets[1][1] = 2;
    }

    if (game->pellets[1][26]) {
        game->pellets[1][26] = 2;
    }

    if (game->pellets[19][1]) {
        game->pellets[19][1] = 2;
    }

    if (game->pellets[19][26]) {
        game->pellets[19][26] = 2;
    }

    /*
     * Remove pellet from Pac-Man starting tile, if one exists.
     */
    int start_row = pixel_to_row(game->pacman.y);
    int start_col = pixel_to_col(game->pacman.x);

    if (start_row >= 0 && start_row < MAZE_ROWS &&
        start_col >= 0 && start_col < MAZE_COLS &&
        game->pellets[start_row][start_col]) {
        game->pellets[start_row][start_col] = 0;
        game->pellets_remaining--;
    }

    /*
     * Remove pellets from ghost starting tiles.
     */
    for (int i = 0; i < GHOST_COUNT; i++) {
        int ghost_row = pixel_to_row(game->ghosts[i].y);
        int ghost_col = pixel_to_col(game->ghosts[i].x);

        if (ghost_row >= 0 && ghost_row < MAZE_ROWS &&
            ghost_col >= 0 && ghost_col < MAZE_COLS &&
            game->pellets[ghost_row][ghost_col]) {
            game->pellets[ghost_row][ghost_col] = 0;
            game->pellets_remaining--;
        }
    }
}

static void collect_pellet(GameState *game)
{
    int row = pixel_to_row(game->pacman.y);
    int col = pixel_to_col(game->pacman.x);

    if (row < 0 || row >= MAZE_ROWS ||
        col < 0 || col >= MAZE_COLS) {
        return;
    }

    if (game->pellets[row][col]) {
        int pellet_type = game->pellets[row][col];

        game->pellets[row][col] = 0;
        game->pellets_remaining--;

        if (pellet_type == 2) {
            game->score += 50;

            game->ghost_mode = GHOST_MODE_FRIGHTENED;
            game->frightened_timer = FRIGHTENED_DURATION_FRAMES;

            /*
             * Classic Pac-Man behavior:
             * ghosts reverse direction when frightened starts.
             */
            for (int i = 0; i < GHOST_COUNT; i++) {
                game->ghosts[i].dir = opposite_dir(game->ghosts[i].dir);
            }
        } else {
            game->score += 10;
        }
        if (game->pellets_remaining <= 0) {
            xil_printf("You win! All pellets eaten.\n\r");

            game->game_won = 1;
            enter_game_over_state(game);
        }
    }
}

// ============================================================================
// INIT
// ============================================================================
static void init_game(GameState *game)
{
    game->game_running = 1;
    game->game_over = 0;
    game->game_won = 0;
    game->score = 0;
    game->lives = STARTING_LIVES;

    /*
     * Ghosts start in scatter mode first.
     */
    game->entering_name = 0;
    game->name_entry_done = 0;
    game->selected_name_char = 0;
    game->leaderboard_entry_index = -1;
    game->game_over_frame_counter = 0;

    game->ghost_mode = GHOST_MODE_SCATTER;
    game->ghost_mode_timer = 0;
    game->frightened_timer = 0;
    game->ghost_random_seed = 1;

    game->current_name[0] = 'A';
    game->current_name[1] = 'A';
    game->current_name[2] = 'A';
    game->current_name[3] = '\0';

    /*
     * Start Pac-Man in the left tunnel opening.
     */
    game->pacman.x = col_to_pixel_center(1);
    game->pacman.y = row_to_pixel_center(10);

    game->pacman.dir = DIR_NONE;
    game->pacman.next_dir = DIR_NONE;

    game->pacman.mouth_counter = 0;
    game->pacman.mouth_phase = 0;

    init_ghosts(game);
    init_pellets(game);
}

// ============================================================================
// INPUT
// ============================================================================
static void update_name_entry(GameState *game, unsigned int pressed_edges)
{
    if (!game->entering_name) {
        return;
    }

    if (pressed_edges & BTN_NEXT_LETTER) {
        if (game->current_name[game->selected_name_char] == 'Z') {
            game->current_name[game->selected_name_char] = 'A';
        } else {
            game->current_name[game->selected_name_char]++;
        }

        update_leaderboard_name(game);
    }

    if (pressed_edges & BTN_PREV_LETTER) {
        if (game->current_name[game->selected_name_char] == 'A') {
            game->current_name[game->selected_name_char] = 'Z';
        } else {
            game->current_name[game->selected_name_char]--;
        }

        update_leaderboard_name(game);
    }

    if (pressed_edges & BTN_NEXT_CHAR) {
        game->selected_name_char++;

        if (game->selected_name_char >= NAME_ENTRY_LENGTH) {
            game->selected_name_char = NAME_ENTRY_LENGTH - 1;
            game->entering_name = 0;
            game->name_entry_done = 1;
        }
    }
}

static void handle_input_zynq(GameState *game)
{
    unsigned int buttons = *(volatile unsigned int *)0x41210000;

    static unsigned int last_buttons = 0xFFFFFFFF;

    if (buttons != last_buttons) {
        xil_printf("buttons = 0x%x\n\r", buttons);
        last_buttons = buttons;
    }

    /*
     * Current mapping:
     * btn[0] = right
     * btn[1] = up
     * btn[2] = down
     * btn[3] = left
     */
    if (buttons & 0x1) {
        game->pacman.next_dir = DIR_RIGHT;
    } else if (buttons & 0x2) {
        game->pacman.next_dir = DIR_UP;
    } else if (buttons & 0x4) {
        game->pacman.next_dir = DIR_DOWN;
    } else if (buttons & 0x8) {
        game->pacman.next_dir = DIR_LEFT;
    }
}

static unsigned int read_buttons_zynq(void)
{
    return (*(volatile unsigned int *)0x41210000) & 0xF;
}

//static int any_button_pressed_zynq(void)
//{
//    return read_buttons_zynq() != 0;
//}

// ============================================================================
// PACMAN UPDATE
// ============================================================================
static void update_pacman(GameState *game)
{
    Pacman *p = &game->pacman;

    /*
     * Only allow turning and wall decisions at tile centers.
     * This keeps Pac-Man aligned to the maze grid.
     */
    if (is_exact_tile_center(p->x, p->y)) {
        int row = pixel_to_row(p->y);
        int col = pixel_to_col(p->x);

        if (can_move_from_tile(row, col, p->next_dir)) {
            p->dir = p->next_dir;
        }

        if (!can_move_from_tile(row, col, p->dir)) {
            p->dir = DIR_NONE;
        }

        collect_pellet(game);
    }

    move_entity(&p->x, &p->y, p->dir, PACMAN_SPEED);

    wrap_tunnel_entity(&p->x, p->y);

    /*
     * Smooth mouth animation.
     * Faster version:
     * closed -> small -> medium -> wide -> medium -> small
     */
    p->mouth_counter++;

    if (p->mouth_counter >= 2) {
        p->mouth_counter = 0;
        p->mouth_phase++;

        if (p->mouth_phase >= 6) {
            p->mouth_phase = 0;
        }
    }
}

// ============================================================================
// GHOST AI
// ============================================================================
static void get_ghost_target(GameState *game, int ghost_index,
                             int *target_row, int *target_col)
{
    Ghost *ghost = &game->ghosts[ghost_index];

    int pac_row = pixel_to_row(game->pacman.y);
    int pac_col = pixel_to_col(game->pacman.x);

    if (game->ghost_mode == GHOST_MODE_SCATTER) {
        *target_row = ghost->scatter_row;
        *target_col = ghost->scatter_col;
        return;
    }

    if (game->ghost_mode == GHOST_MODE_FRIGHTENED) {
        *target_row = ghost->scatter_row;
        *target_col = ghost->scatter_col;
        return;
    }

    /*
     * Chase mode.
     */
    if (ghost_index == GHOST_BLINKY) {
        /*
         * Blinky targets Pac-Man directly.
         */
        *target_row = pac_row;
        *target_col = pac_col;
    } else if (ghost_index == GHOST_PINKY) {
        /*
         * Pinky targets four tiles ahead of Pac-Man.
         */
        *target_row = pac_row + direction_dy(game->pacman.dir) * 4;
        *target_col = pac_col + direction_dx(game->pacman.dir) * 4;
    } else if (ghost_index == GHOST_INKY) {
        /*
         * Inky uses both Pac-Man and Blinky.
         */
        Ghost *blinky = &game->ghosts[GHOST_BLINKY];

        int blinky_row = pixel_to_row(blinky->y);
        int blinky_col = pixel_to_col(blinky->x);

        int ahead_row = pac_row + direction_dy(game->pacman.dir) * 2;
        int ahead_col = pac_col + direction_dx(game->pacman.dir) * 2;

        *target_row = ahead_row + (ahead_row - blinky_row);
        *target_col = ahead_col + (ahead_col - blinky_col);
    } else {
        /*
         * Clyde targets Pac-Man when far away.
         * If close, he retreats to bottom-left.
         */
        int ghost_row = pixel_to_row(ghost->y);
        int ghost_col = pixel_to_col(ghost->x);

        int dist_sq = tile_distance_sq(ghost_row, ghost_col,
                                       pac_row, pac_col);

        if (dist_sq <= 64) {
            *target_row = ghost->scatter_row;
            *target_col = ghost->scatter_col;
        } else {
            *target_row = pac_row;
            *target_col = pac_col;
        }
    }
}

static void update_ghost_mode(GameState *game)
{
    if (game->ghost_mode == GHOST_MODE_FRIGHTENED) {
        if (game->frightened_timer > 0) {
            game->frightened_timer--;
        }

        if (game->frightened_timer <= 0) {
            game->frightened_timer = 0;
            game->ghost_mode = GHOST_MODE_CHASE;
            game->ghost_mode_timer = 0;

            /*
             * Frightened mode is ending.
             *
             * If a ghost is between tile centers, do not immediately switch
             * it from speed 3 to speed 6. Instead, let that individual ghost
             * continue at speed 3 until it reaches its next tile center.
             */
            for (int i = 0; i < GHOST_COUNT; i++) {
                if (is_exact_tile_center(game->ghosts[i].x,
                                         game->ghosts[i].y)) {
                    game->ghosts[i].slow_until_center = 0;
                } else {
                    game->ghosts[i].slow_until_center = 1;
                }

                game->ghosts[i].dir = opposite_dir(game->ghosts[i].dir);
            }
        }

        return;
    }

    game->ghost_mode_timer++;

    if (game->ghost_mode == GHOST_MODE_SCATTER) {
        if (game->ghost_mode_timer >= SCATTER_DURATION_FRAMES) {
            game->ghost_mode = GHOST_MODE_CHASE;
            game->ghost_mode_timer = 0;

            for (int i = 0; i < GHOST_COUNT; i++) {
                game->ghosts[i].dir = opposite_dir(game->ghosts[i].dir);
            }
        }
    } else {
        if (game->ghost_mode_timer >= CHASE_DURATION_FRAMES) {
            game->ghost_mode = GHOST_MODE_SCATTER;
            game->ghost_mode_timer = 0;

            for (int i = 0; i < GHOST_COUNT; i++) {
                game->ghosts[i].dir = opposite_dir(game->ghosts[i].dir);
            }
        }
    }
}


static Direction choose_ghost_direction(GameState *game, int ghost_index)
{
    Ghost *ghost = &game->ghosts[ghost_index];

    int row = pixel_to_row(ghost->y);
    int col = pixel_to_col(ghost->x);

    Direction options[4];
    int option_count = 0;

    /*
     * This order is useful for tie-breaking.
     */
    Direction dirs[4] = {
        DIR_UP,
        DIR_LEFT,
        DIR_DOWN,
        DIR_RIGHT
    };

    /*
     * First pass:
     * avoid reversing direction unless there is no choice.
     */
    for (int i = 0; i < 4; i++) {
        Direction dir = dirs[i];

        if (dir == opposite_dir(ghost->dir)) {
            continue;
        }

        if (can_move_from_tile(row, col, dir)) {
            options[option_count] = dir;
            option_count++;
        }
    }

    /*
     * Second pass:
     * allow reverse direction if trapped.
     */
    if (option_count == 0) {
        for (int i = 0; i < 4; i++) {
            Direction dir = dirs[i];

            if (can_move_from_tile(row, col, dir)) {
                options[option_count] = dir;
                option_count++;
            }
        }
    }

    if (option_count == 0) {
        return DIR_NONE;
    }

    if (game->ghost_mode == GHOST_MODE_FRIGHTENED) {
        int index = ghost_random(game) % option_count;
        return options[index];
    }

    int target_row;
    int target_col;

    get_ghost_target(game, ghost_index, &target_row, &target_col);

    Direction best_dir = options[0];
    int best_dist = 0x7FFFFFFF;

    for (int i = 0; i < option_count; i++) {
        Direction dir = options[i];

        int next_row = row + direction_dy(dir);
        int next_col = col + direction_dx(dir);

        int dist = tile_distance_sq(next_row, next_col,
                                    target_row, target_col);

        if (dist < best_dist) {
            best_dist = dist;
            best_dir = dir;
        }
    }

    return best_dir;
}

static void update_ghost(GameState *game, int ghost_index)
{
    Ghost *ghost = &game->ghosts[ghost_index];

    if (is_exact_tile_center(ghost->x, ghost->y)) {
        int row = pixel_to_row(ghost->y);
        int col = pixel_to_col(ghost->x);

        /*
         * If frightened mode has ended and this ghost was waiting to realign,
         * it is now safe to return to normal speed.
         */
        if (ghost->slow_until_center) {
            ghost->slow_until_center = 0;
        }

        ghost->dir = choose_ghost_direction(game, ghost_index);

        if (!can_move_from_tile(row, col, ghost->dir)) {
            ghost->dir = DIR_NONE;
        }
    }

    int speed;

    if (game->ghost_mode == GHOST_MODE_FRIGHTENED ||
        ghost->slow_until_center) {
        speed = GHOST_FRIGHTENED_SPEED;
    } else {
        speed = GHOST_SPEED;
    }

    move_entity(&ghost->x,
                &ghost->y,
                ghost->dir,
                speed);

    wrap_tunnel_entity(&ghost->x, ghost->y);
}

static void update_ghosts(GameState *game)
{
    for (int i = 0; i < GHOST_COUNT; i++) {
        update_ghost(game, i);
    }
}

// ============================================================================
// COLLISIONS
// ============================================================================
static void check_ghost_collisions(GameState *game)
{
    for (int i = 0; i < GHOST_COUNT; i++) {
        Ghost *ghost = &game->ghosts[i];

        int dx = game->pacman.x - ghost->x;
        int dy = game->pacman.y - ghost->y;

        int collision_dist = PACMAN_RADIUS + GHOST_RADIUS;

        if (dx * dx + dy * dy <= collision_dist * collision_dist) {
            if (game->ghost_mode == GHOST_MODE_FRIGHTENED) {
                game->score += 200;
                reset_ghost_to_home(game, i);
            } else {
                game->lives--;

                xil_printf("Pac-Man was caught! Lives left: %d\n\r",
                           game->lives);

                if (game->lives <= 0) {
                    xil_printf("Game over!\n\r");

                    game->lives = 0;
                    game->game_won = 0;
                    enter_game_over_state(game);
                } else {
                    respawn_after_death(game);
                }

                return;
            }
        }
    }
}

// ============================================================================
// GAME UPDATE
// ============================================================================
static void update_game(GameState *game)
{
    static unsigned int previous_buttons = 0;
    static int restart_waiting_for_release = 0;

    unsigned int buttons = read_buttons_zynq();
    unsigned int pressed_edges = buttons & ~previous_buttons;

    previous_buttons = buttons;

    if (!game->game_running) {
        return;
    }

    if (game->game_over) {
        game->game_over_frame_counter++;

        /*
         * While entering initials:
         * button 1 = next letter
         * button 2 = previous letter
         * button 3 = next character / finish
         */
        if (game->entering_name) {
            update_name_entry(game, pressed_edges);

            if (game->name_entry_done) {
                restart_waiting_for_release = 1;
            }

            return;
        }

        /*
         * After name entry is done, wait for all buttons to be released.
         * Then any new button press restarts the game.
         */
        if (game->name_entry_done) {
            if (restart_waiting_for_release) {
                if (buttons == 0) {
                    restart_waiting_for_release = 0;
                }

                return;
            }

            if (pressed_edges != 0) {
                init_game(game);
                previous_buttons = 0;
                restart_waiting_for_release = 0;
            }

            return;
        }

        return;
    }

    restart_waiting_for_release = 0;

    update_ghost_mode(game);

    update_pacman(game);

    update_ghosts(game);

    check_ghost_collisions(game);
}

// ============================================================================
// RENDER
// ============================================================================
static void render_maze(void)
{
    for (int row = 0; row < MAZE_ROWS; row++) {
        for (int col = 0; col < MAZE_COLS; col++) {
            int x = MAZE_OFFSET_X + col * TILE_SIZE;
            int y = MAZE_OFFSET_Y + row * TILE_SIZE;

            if (maze[row][col] == '#') {
                draw_rect(x, y, TILE_SIZE, TILE_SIZE, BLUE_R, BLUE_G, BLUE_B);
            } else {
                draw_rect(x, y, TILE_SIZE, TILE_SIZE, BLACK_R, BLACK_G, BLACK_B);
            }
        }
    }
}

static void render_pellets(GameState *game)
{
    for (int row = 0; row < MAZE_ROWS; row++) {
        for (int col = 0; col < MAZE_COLS; col++) {
            if (game->pellets[row][col] == 1) {
                draw_filled_circle(col_to_pixel_center(col),
                                   row_to_pixel_center(row),
                                   PELLET_RADIUS,
                                   WHITE_R, WHITE_G, WHITE_B);
            } else if (game->pellets[row][col] == 2) {
                draw_filled_circle(col_to_pixel_center(col),
                                   row_to_pixel_center(row),
                                   POWER_PELLET_RADIUS,
                                   WHITE_R, WHITE_G, WHITE_B);
            }
        }
    }
}

static void render_ghosts(GameState *game)
{
    for (int i = 0; i < GHOST_COUNT; i++) {
        Ghost *ghost = &game->ghosts[i];

        if (game->ghost_mode == GHOST_MODE_FRIGHTENED) {
            draw_ghost(ghost->x,
                       ghost->y,
                       FRIGHT_R,
                       FRIGHT_G,
                       FRIGHT_B);
        } else {
            draw_ghost(ghost->x,
                       ghost->y,
                       ghost->r,
                       ghost->g,
                       ghost->b);
        }
    }
}

static void render_game_hdmi(GameState *game)
{
    /*
     * Clear entire screen black.
     */
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        renderer_draw_grey_row(0, y, SCREEN_WIDTH, 0);
    }

    draw_score(game);
    draw_lives(game);
    draw_leaderboard(game);
    if (game->game_over) {
        draw_end_message(game);
    }

    render_maze();
    render_pellets(game);
    render_ghosts(game);

    draw_pacman(game->pacman.x,
                game->pacman.y,
                PACMAN_RADIUS,
                game->pacman.dir,
                game->pacman.mouth_phase);

    static int print_counter = 0;
    print_counter++;

    if (print_counter % 60 == 0) {
        xil_printf("Pacman pixel=(%d,%d) tile=(%d,%d) dir=%d next=%d pellets=%d mode=%d lives=%d\n\r",
                   game->pacman.x,
                   game->pacman.y,
                   pixel_to_row(game->pacman.y),
                   pixel_to_col(game->pacman.x),
                   game->pacman.dir,
                   game->pacman.next_dir,
                   game->pellets_remaining,
                   game->ghost_mode,
                   game->lives);
    }
}

// ============================================================================
// MAIN LOOP
// ============================================================================
void pacman_game_run(void)
{
    GameState game;
    init_game(&game);

    xil_printf("==============================================\n\r");
    xil_printf("Zynq Pac-Man With Ghosts And Lives\n\r");
    xil_printf("==============================================\n\r");
    xil_printf("Screen: %d x %d\n\r", SCREEN_WIDTH, SCREEN_HEIGHT);
    xil_printf("Maze: %d x %d tiles\n\r", MAZE_COLS, MAZE_ROWS);
    xil_printf("Tile size: %d px\n\r", TILE_SIZE);
    xil_printf("Pacman speed: %d px/frame\n\r", PACMAN_SPEED);
    xil_printf("Ghost speed: %d px/frame\n\r", GHOST_SPEED);
    xil_printf("Lives: %d\n\r", game.lives);
    xil_printf("Pellets: %d\n\r", game.pellets_remaining);

    long frame_counter = 0;
    int debug_update_interval = 3;

    while (game.game_running) {
        profiler_start(&profiler_pacman[0]);

        profiler_start(&profiler_pacman[1]);
        handle_input_zynq(&game);
        profiler_end(&profiler_pacman[1]);

        profiler_start(&profiler_pacman[2]);
        update_game(&game);
        profiler_end(&profiler_pacman[2]);

        profiler_start(&profiler_pacman[3]);
        render_game_hdmi(&game);
        profiler_end(&profiler_pacman[3]);

        profiler_start(&profiler_pacman[4]);
        renderer_render(100);
        profiler_end(&profiler_pacman[4]);

        frame_counter++;

        if (frame_counter % (FPS * debug_update_interval) == 0) {
            xil_printf("Frame: %lu\n\r", frame_counter);
            xil_printf("Input time: %lu us\n\r", profiler_pacman[1].elapsed_us);
            xil_printf("Update time: %lu us\n\r", profiler_pacman[2].elapsed_us);
            xil_printf("Render time: %lu us\n\r", profiler_pacman[3].elapsed_us);
            xil_printf("Display time: %lu us\n\r", profiler_pacman[4].elapsed_us);
            xil_printf("\n\r");
        }

        profiler_end(&profiler_pacman[0]);

        if (profiler_pacman[0].elapsed_us < FRAME_DELAY_US) {
            usleep(FRAME_DELAY_US - profiler_pacman[0].elapsed_us);
        }
    }

    xil_printf("\n\rPac-Man ended.\n\r");
}
