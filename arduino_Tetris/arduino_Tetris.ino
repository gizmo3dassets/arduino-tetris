/*
 * ╔══════════════════════════════════════════════════════╗
 * ║  TETRIS  –  SSD1306 128×64  (PORTRAIT 64×128)        ║
 * ║  U8g2 · bitmap splash · EEPROM high score            ║
 * ╠══════════════════════════════════════════════════════╣
 * ║  Author  : Cortex                                    ║
 * ║  Hardware: Arduino Nano · SSD1306 OLED · Passive     ║
 * ║            buzzer · 5× tactile buttons               ║
 * ╚══════════════════════════════════════════════════════╝
 *
 * Requires bitmaps.h and pitches.h in the same folder.
 *
 * Display is mounted in portrait orientation via U8G2_R3
 * (270° rotation), giving a logical canvas of 64×128 px.
 *
 * ── LAYOUT ──────────────────────────────────────────────
 *  ┌─────────────────────────────────────┐  y=0
 *  │         SCORE        │  NEXT PIECE  │  y=0..12
 *  ├─────────────────────────────────────┤  y=13
 *  │         BOARD  10×19 cells          │  y=15..128
 *  └─────────────────────────────────────┘
 *
 * ── WIRING ──────────────────────────────────────────────
 *  SSD1306   SDA → A4   SCL → A5   VCC → 5V   GND → GND
 *  RIGHT   → D4   (INPUT_PULLUP, other leg GND)
 *  LEFT    → D5
 *  DOWN    → D6   (double-tap = hard drop)
 *  ROTATE  → D7
 *  PAUSE   → D8   (doubles as START on splash/game over)
 *  BUZZER  → D13
 *
 * ── LIBRARIES ───────────────────────────────────────────
 *  U8g2  (Arduino Library Manager)
 */

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <EEPROM.h>
#include "bitmaps.h"
#include "pitches.h"

// ═══════════════════════════════════════════════════════
// DISPLAY
// ═══════════════════════════════════════════════════════

// Full framebuffer mode (_F_) required for clearBuffer/sendBuffer.
// R3 = 270° rotation → logical 64 wide × 128 tall.
U8G2_SSD1306_128X64_NONAME_F_HW_I2C
  u8g2(U8G2_R3, /* reset=*/ U8X8_PIN_NONE);

#define L_W  64    // logical canvas width  (px)
#define L_H 128    // logical canvas height (px)

// ═══════════════════════════════════════════════════════
// PINS
// ═══════════════════════════════════════════════════════

#define BTN_RIGHT  4
#define BTN_LEFT   5
#define BTN_DOWN   6   // double-tap for hard drop
#define BTN_ROTATE 7
#define BTN_PAUSE  8   // also START on splash / game over
#define BUZZER    13

inline void beep(uint16_t freq, uint16_t dur) { tone(BUZZER, freq, dur); }

// ═══════════════════════════════════════════════════════
// LAYOUT CONSTANTS
// ═══════════════════════════════════════════════════════

#define DIVIDER_Y 13   // y of horizontal line below header

#define CELL       6   // cell size in pixels
#define COLS      10   // board columns
#define ROWS      19   // board rows
#define BOARD_X    2   // board left edge (px)
#define BOARD_Y   15   // board top edge  (px)

// Pixel origin of cell (col, row)
#define BX(c)  (BOARD_X + (c) * CELL)
#define BY(r)  (BOARD_Y + (r) * CELL)

// ═══════════════════════════════════════════════════════
// EEPROM
// ═══════════════════════════════════════════════════════

#define EEPROM_ADDR 0   // base address for uint32_t high score (4 bytes)

// ═══════════════════════════════════════════════════════
// TIMING CONSTANTS
// ═══════════════════════════════════════════════════════

#define DAS_DELAY     170   // ms before auto-repeat kicks in
#define DAS_REPEAT     50   // ms between auto-repeat steps
#define LOCK_DELAY    500   // ms grace period before piece locks
#define DOUBLE_TAP    250   // ms window for double-tap hard drop

// ═══════════════════════════════════════════════════════
// TETROMINOES
// ═══════════════════════════════════════════════════════
// 7 pieces × 4 rotations × 4 cells × (row, col) offset.
// Stored in flash to save RAM.

const int8_t PIECES[7][4][4][2] PROGMEM = {
  // I
  {{{0,0},{0,1},{0,2},{0,3}},{{0,2},{1,2},{2,2},{3,2}},{{1,0},{1,1},{1,2},{1,3}},{{0,1},{1,1},{2,1},{3,1}}},
  // O
  {{{0,0},{0,1},{1,0},{1,1}},{{0,0},{0,1},{1,0},{1,1}},{{0,0},{0,1},{1,0},{1,1}},{{0,0},{0,1},{1,0},{1,1}}},
  // T
  {{{0,1},{1,0},{1,1},{1,2}},{{0,1},{1,1},{1,2},{2,1}},{{1,0},{1,1},{1,2},{2,1}},{{0,1},{1,0},{1,1},{2,1}}},
  // S
  {{{0,1},{0,2},{1,0},{1,1}},{{0,1},{1,1},{1,2},{2,2}},{{1,1},{1,2},{2,0},{2,1}},{{0,0},{1,0},{1,1},{2,1}}},
  // Z
  {{{0,0},{0,1},{1,1},{1,2}},{{0,2},{1,1},{1,2},{2,1}},{{1,0},{1,1},{2,1},{2,2}},{{0,1},{1,0},{1,1},{2,0}}},
  // J
  {{{0,0},{1,0},{1,1},{1,2}},{{0,1},{0,2},{1,1},{2,1}},{{1,0},{1,1},{1,2},{2,2}},{{0,1},{1,1},{2,0},{2,1}}},
  // L
  {{{0,2},{1,0},{1,1},{1,2}},{{0,1},{1,1},{2,1},{2,2}},{{1,0},{1,1},{1,2},{2,0}},{{0,0},{0,1},{1,1},{2,1}}}
};

// ═══════════════════════════════════════════════════════
// INTRO MELODY  (Tetris Theme A, non-blocking)
// ═══════════════════════════════════════════════════════

struct Note { uint16_t freq; uint16_t dur; };

const Note INTRO[] PROGMEM = {
  {NOTE_E5,220},{NOTE_B4,110},{NOTE_C5,110},{NOTE_D5,220},{NOTE_C5,110},{NOTE_B4,110},
  {NOTE_A4,220},{NOTE_A4,110},{NOTE_C5,110},{NOTE_E5,220},{NOTE_D5,110},{NOTE_C5,110},
  {NOTE_B4,330},{NOTE_C5,110},{NOTE_D5,220},{NOTE_E5,220},{NOTE_C5,220},{NOTE_A4,220},{NOTE_A4,220},
  {0,110},
  {NOTE_D5,220},{NOTE_F5,110},{NOTE_A5,220},{NOTE_G5,110},{NOTE_F5,110},
  {NOTE_E5,330},{NOTE_C5,110},{NOTE_E5,220},{NOTE_D5,110},{NOTE_C5,110},
  {NOTE_B4,220},{NOTE_B4,110},{NOTE_C5,110},{NOTE_D5,220},{NOTE_E5,220},
  {NOTE_C5,220},{NOTE_A4,220},{NOTE_A4,220},
  {0,140},
  {NOTE_A4,100},{NOTE_C5,100},{NOTE_E5,240}
};
#define INTRO_LEN (sizeof(INTRO) / sizeof(INTRO[0]))

// Melody playback state
uint8_t  introNote    = 0;
uint32_t noteStart    = 0;
bool     introPlaying = false;

// Begin playback from the first note.
void startIntro() {
  introNote    = 0;
  noteStart    = millis();
  introPlaying = true;
  uint16_t f   = pgm_read_word(&INTRO[0].freq);
  uint16_t d   = pgm_read_word(&INTRO[0].dur);
  if (f) tone(BUZZER, f, d); else noTone(BUZZER);
}

// Advance to the next note when the current one has elapsed.
// Call once per loop iteration — never blocks.
void updateIntro() {
  if (!introPlaying) return;
  uint16_t d = pgm_read_word(&INTRO[introNote].dur);
  if (millis() - noteStart >= (uint32_t)(d * 11 / 10)) {
    introNote++;
    if (introNote >= INTRO_LEN) {
      introPlaying = false;
      noTone(BUZZER);
      return;
    }
    noteStart    = millis();
    uint16_t f   = pgm_read_word(&INTRO[introNote].freq);
    d            = pgm_read_word(&INTRO[introNote].dur);
    if (f) tone(BUZZER, f, d); else noTone(BUZZER);
  }
}

// ═══════════════════════════════════════════════════════
// GAME STATE
// ═══════════════════════════════════════════════════════

uint8_t  board[ROWS][COLS];   // 0 = empty, 1 = locked cell
int8_t   pieceType, nextType;
int8_t   pieceRot, pieceRow, pieceCol;
uint32_t score;
uint32_t highScore;
uint16_t lines;
uint8_t  level;
bool     gameOver, paused;
uint32_t lastFall, fallInterval;

// Back-to-back Tetris tracking
bool     lastWasTetris = false;

// Lock delay state
uint32_t lockTimer    = 0;   // when the piece first touched the floor
bool     onGround     = false;

// Auto-repeat (DAS) state
uint32_t dasTimer     = 0;   // when the key was first held
uint32_t dasRepeat    = 0;   // last repeat fire time
bool     dasLeft      = false;
bool     dasRight     = false;

// Double-tap hard drop state
uint32_t lastDownTap  = 0;   // time of last DOWN press
bool     downWasUp    = true; // tracks rising edge for tap detection

// Current and previous button states (for edge detection)
bool bL, bR, bD, bRot, bPau;
bool pL, pR, pD, pRot, pPau;

// ═══════════════════════════════════════════════════════
// PIECE HELPERS
// ═══════════════════════════════════════════════════════

// Populate out[4][2] with the absolute (row, col) of each cell
// for the given piece type, rotation, and board position.
void getCells(int8_t type, int8_t rot, int8_t r, int8_t c, int8_t out[4][2]) {
  for (uint8_t i = 0; i < 4; i++) {
    out[i][0] = r + (int8_t)pgm_read_byte(&PIECES[type][rot][i][0]);
    out[i][1] = c + (int8_t)pgm_read_byte(&PIECES[type][rot][i][1]);
  }
}

// Return true if the piece fits at the given position.
bool valid(int8_t type, int8_t rot, int8_t r, int8_t c) {
  int8_t cells[4][2];
  getCells(type, rot, r, c, cells);
  for (uint8_t i = 0; i < 4; i++) {
    int8_t nr = cells[i][0], nc = cells[i][1];
    if (nr < 0 || nr >= ROWS || nc < 0 || nc >= COLS) return false;
    if (board[nr][nc]) return false;
  }
  return true;
}

// Write the active piece into the board array.
void lockPiece() {
  int8_t cells[4][2];
  getCells(pieceType, pieceRot, pieceRow, pieceCol, cells);
  for (uint8_t i = 0; i < 4; i++) board[cells[i][0]][cells[i][1]] = 1;
  beep(180, 35);
}

// Remove completed rows and return the count cleared.
uint8_t sweepLines() {
  uint8_t n = 0;
  for (int8_t r = ROWS - 1; r >= 0; r--) {
    bool full = true;
    for (uint8_t c = 0; c < COLS; c++) if (!board[r][c]) { full = false; break; }
    if (full) {
      for (int8_t rr = r; rr > 0; rr--) memcpy(board[rr], board[rr-1], COLS);
      memset(board[0], 0, COLS);
      r++; n++;
    }
  }
  return n;
}

// Update score and fall speed after a line clear.
// Applies back-to-back Tetris bonus (1.5×) and hard drop
// bonuses are added at the call site.
void applyScore(uint8_t n) {
  if (!n) return;

  const uint16_t pts[5] = {0, 40, 100, 300, 1200};
  uint32_t base = (uint32_t)pts[n] * (level + 1);

  // Back-to-back Tetris: 50% bonus if previous clear was also a Tetris
  if (n == 4 && lastWasTetris) base += base / 2;
  lastWasTetris = (n == 4);

  score       += base;
  lines       += n;
  level        = lines / 10;
  fallInterval = max(80UL, 800UL - (uint32_t)level * 70);

  if      (n == 4) { beep(880, 60); delay(70); beep(1100, 130); }
  else if (n >= 2) { beep(660, 55); delay(65); beep(880,  110); }
  else             { beep(523, 90); }
}

// ═══════════════════════════════════════════════════════
// DRAWING HELPERS
// ═══════════════════════════════════════════════════════

// Solid cell (locked pieces and active piece body)
void drawFilledCell(int16_t px, int16_t py) {
  u8g2.drawBox(px, py, CELL - 1, CELL - 1);
}

// Outline-only cell (ghost piece)
void drawOutlineCell(int16_t px, int16_t py) {
  u8g2.drawFrame(px, py, CELL - 1, CELL - 1);
}

// ── Header ────────────────────────────────────────────
// Draws the outer border, horizontal divider, and two
// zones: score | next-piece preview
void drawScoreBox() {
  u8g2.drawFrame(0, 0, L_W, L_H);
  u8g2.drawHLine(0, DIVIDER_Y, L_W);
  u8g2.drawVLine(48, 0, DIVIDER_Y);

  u8g2.setFont(u8g2_font_tom_thumb_4x6_tr);

  // Zone A: score (x=0..47), left-aligned
  char sc[7];
  if      (score >= 10000000UL) snprintf(sc, sizeof(sc), "%4luM", score/1000000UL);
  else if (score >=   100000UL) snprintf(sc, sizeof(sc), "%4luK", score/1000UL);
  else                          snprintf(sc, sizeof(sc), "%-6lu",  score);
  u8g2.setCursor(3, 10);
  u8g2.print(sc);

  // Zone B: next piece, centred in x=49..63, y=0..12
  int8_t cells[4][2];
  getCells(nextType, 0, 0, 0, cells);

  int8_t minR=3, maxR=0, minC=3, maxC=0;
  for (uint8_t i = 0; i < 4; i++) {
    if (cells[i][0] < minR) minR = cells[i][0];
    if (cells[i][0] > maxR) maxR = cells[i][0];
    if (cells[i][1] < minC) minC = cells[i][1];
    if (cells[i][1] > maxC) maxC = cells[i][1];
  }
  uint8_t pieceW = (maxC - minC + 1) * 3 - 1;
  uint8_t pieceH = (maxR - minR + 1) * 3 - 1;
  int8_t  offX   = 49 + (15 - pieceW + 1) / 2;
  int8_t  offY   =      (12 - pieceH + 1) / 2;

  for (uint8_t i = 0; i < 4; i++) {
    int16_t px = offX + (cells[i][1] - minC) * 3;
    int16_t py = offY + (cells[i][0] - minR) * 3;
    u8g2.drawBox(px, py, 2, 2);
  }
}

// ── Board ─────────────────────────────────────────────

// Draw all locked cells.
void drawCells() {
  for (uint8_t r = 0; r < ROWS; r++)
    for (uint8_t c = 0; c < COLS; c++)
      if (board[r][c]) drawFilledCell(BX(c), BY(r));
}

// Draw the active (falling) piece.
// A black pixel at (px+1, py+1) gives each cell a subtle dot texture.
void drawActive() {
  int8_t cells[4][2];
  getCells(pieceType, pieceRot, pieceRow, pieceCol, cells);
  for (uint8_t i = 0; i < 4; i++) {
    int16_t px = BX(cells[i][1]), py = BY(cells[i][0]);
    drawFilledCell(px, py);
    u8g2.setDrawColor(0);
    u8g2.drawPixel(px + 1, py + 1);
    u8g2.setDrawColor(1);
  }
}

// Draw the ghost piece (drop preview) as outlines.
void drawGhost() {
  int8_t gr = pieceRow;
  while (valid(pieceType, pieceRot, gr + 1, pieceCol)) gr++;
  if (gr == pieceRow) return;
  int8_t cells[4][2];
  getCells(pieceType, pieceRot, gr, pieceCol, cells);
  for (uint8_t i = 0; i < 4; i++)
    drawOutlineCell(BX(cells[i][1]), BY(cells[i][0]));
}

// ── Overlays ──────────────────────────────────────────

void drawPauseOverlay() {
  u8g2.setDrawColor(0);
  u8g2.drawBox(14, 55, 36, 14);
  u8g2.setDrawColor(1);
  u8g2.drawFrame(14, 55, 36, 14);
  u8g2.setFont(u8g2_font_tom_thumb_4x6_tr);
  u8g2.setCursor(21, 65);
  u8g2.print(F("PAUSED"));
}

// ── Full-screen renders ───────────────────────────────

void renderFrame() {
  u8g2.clearBuffer();
  drawScoreBox();
  drawCells();
  drawGhost();
  drawActive();
  if (paused) drawPauseOverlay();
  u8g2.sendBuffer();
}

void drawGameOver(bool showRetry) {
  u8g2.clearBuffer();

  // Large "GAME OVER" title
  u8g2.setFont(u8g2_font_logisoso16_tr);
  u8g2.setCursor(13, 20);  u8g2.print(F("GAME"));
  u8g2.setCursor(13, 42);  u8g2.print(F("OVER"));

  u8g2.drawHLine(0, 47, L_W);

  // Score
  u8g2.setFont(u8g2_font_tom_thumb_4x6_tr);
  u8g2.setCursor(2, 56);   u8g2.print(F("SCORE"));
  char sc[9];
  snprintf(sc, sizeof(sc), "%lu", score);
  u8g2.setCursor(2, 64);   u8g2.print(sc);

  // High score (framed box)
  u8g2.drawFrame(0, 68, L_W, 18);
  u8g2.setCursor(2, 75);   u8g2.print(F("BEST"));
  char hs[9];
  snprintf(hs, sizeof(hs), "%lu", highScore);
  u8g2.setCursor(2, 83);   u8g2.print(hs);

  u8g2.drawHLine(0, 90, L_W);
  if (showRetry) {
    u8g2.setCursor(20, 110);
    u8g2.print(F("RETRY?"));
  }

  u8g2.sendBuffer();
}

void drawSplash(bool showPrompt) {
  u8g2.clearBuffer();
  u8g2.drawXBMP(0, 30, 64, 64, bmp_Tetris);
  u8g2.drawFrame(0, 0, L_W, L_H);
  u8g2.drawHLine(4, 24, 56);

  u8g2.setFont(u8g2_font_logisoso16_tr);
  u8g2.setCursor(4, 22);
  u8g2.print(F("TETRIS"));

  if (showPrompt) {
    u8g2.setFont(u8g2_font_tom_thumb_4x6_tr);
    u8g2.setCursor(10, 108);
    u8g2.print(F("PRESS START"));
  }

  u8g2.sendBuffer();
}

// ═══════════════════════════════════════════════════════
// GAME OVER TRIGGER
// ═══════════════════════════════════════════════════════
// Separated from spawn() for clarity.
// Saves high score, plays fill animation, sets gameOver flag.

void triggerGameOver() {
  gameOver = true;

  // Persist high score before animation in case of power loss
  if (score > highScore) {
    highScore = score;
    EEPROM.put(EEPROM_ADDR, highScore);
  }

  // Fill board bottom-to-top with descending tone
  for (int r = ROWS - 1; r >= 0; r--) {
    for (uint8_t c = 0; c < COLS; c++) board[r][c] = 1;
    int f = 800 - (ROWS - 1 - r) * 35;
    beep(f, 10);
    u8g2.clearBuffer();
    drawScoreBox();
    drawCells();
    u8g2.sendBuffer();
    delay(10);
  }
  noTone(BUZZER);
}

// ═══════════════════════════════════════════════════════
// SPAWN
// ═══════════════════════════════════════════════════════
// Promotes nextType to the active piece and picks a new nextType.
// Calls triggerGameOver() if the spawn position is blocked.

void spawn() {
  pieceType  = nextType;
  nextType   = random(7);
  pieceRot   = 0;
  pieceRow   = 0;
  pieceCol   = COLS / 2 - 2;
  onGround   = false;
  lockTimer  = 0;

  if (!valid(pieceType, pieceRot, pieceRow, pieceCol)) {
    triggerGameOver();
  }
}

// ═══════════════════════════════════════════════════════
// HARD DROP
// ═══════════════════════════════════════════════════════
// Instantly drops the piece to the lowest valid row,
// awards 2pts per cell dropped, then locks.

void hardDrop() {
  uint8_t dropped = 0;
  while (valid(pieceType, pieceRot, pieceRow + 1, pieceCol)) {
    pieceRow++;
    dropped++;
  }
  score += dropped * 2;   // 2 pts per cell hard dropped
  beep(300, 40);
  lockPiece();
  applyScore(sweepLines());
  spawn();
  // Reset DAS and lock state after hard drop
  onGround  = false;
  dasLeft   = false;
  dasRight  = false;
}

// ═══════════════════════════════════════════════════════
// RESET
// ═══════════════════════════════════════════════════════

void resetGame() {
  memset(board, 0, sizeof(board));
  score          = 0;
  lines          = 0;
  level          = 0;
  fallInterval   = 800;
  gameOver       = false;
  paused         = false;
  lastWasTetris  = false;
  onGround       = false;
  dasLeft        = false;
  dasRight       = false;
  lastDownTap    = 0;
  downWasUp      = true;
  nextType       = random(7);
  spawn();
  lastFall = millis();
  pL = pR = pD = pRot = pPau = false;
}

// ═══════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════

void setup() {
  pinMode(BTN_LEFT,   INPUT_PULLUP);
  pinMode(BTN_RIGHT,  INPUT_PULLUP);
  pinMode(BTN_DOWN,   INPUT_PULLUP);
  pinMode(BTN_ROTATE, INPUT_PULLUP);
  pinMode(BTN_PAUSE,  INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);
  randomSeed(analogRead(A1));

  u8g2.begin();
  u8g2.setContrast(255);

  // Load persisted high score (0xFFFFFFFF = uninitialised EEPROM)
  EEPROM.get(EEPROM_ADDR, highScore);
  if (highScore == 0xFFFFFFFF) highScore = 0;

  // ── Splash loop ───────────────────────────────────────
  // Non-blocking melody plays while splash is displayed.
  // PAUSE starts the game.
  bool     showPrompt = true;
  uint32_t lastBlink  = 0;

  startIntro();

  while (digitalRead(BTN_PAUSE) == HIGH) {
    uint32_t now = millis();
    updateIntro();
    if (now - lastBlink >= 500) {
      lastBlink  = now;
      showPrompt = !showPrompt;
    }
    drawSplash(showPrompt);
  }

  noTone(BUZZER);
  delay(220);   // debounce
  resetGame();
}

// ═══════════════════════════════════════════════════════
// MAIN LOOP
// ═══════════════════════════════════════════════════════

void loop() {

  // ── Game over ─────────────────────────────────────────
  if (gameOver) {
    static bool     showRetry = true;
    static uint32_t lastBlink = 0;
    uint32_t now = millis();
    if (now - lastBlink >= 500) {
      lastBlink = now;
      showRetry = !showRetry;
    }
    drawGameOver(showRetry);
    if (digitalRead(BTN_PAUSE) == LOW) { delay(300); resetGame(); }
    return;
  }

  // ── Read buttons ──────────────────────────────────────
  bL   = (digitalRead(BTN_LEFT)   == LOW);
  bR   = (digitalRead(BTN_RIGHT)  == LOW);
  bD   = (digitalRead(BTN_DOWN)   == LOW);
  bRot = (digitalRead(BTN_ROTATE) == LOW);
  bPau = (digitalRead(BTN_PAUSE)  == LOW);

  uint32_t now = millis();

  // ── Pause toggle ──────────────────────────────────────
  if (bPau && !pPau) {
    paused = !paused;
    beep(paused ? 280 : 520, 70);
    if (!paused) lastFall = millis();
  }
  pPau = bPau;

  if (paused) { renderFrame(); delay(20); return; }

  // ── Double-tap hard drop ──────────────────────────────
  // Two DOWN presses within DOUBLE_TAP ms triggers hard drop.
  if (bD && !pD) {
    if (downWasUp) {
      if (now - lastDownTap < DOUBLE_TAP) {
        hardDrop();
        pL = bL; pR = bR; pD = bD; pRot = bRot;
        renderFrame();
        return;
      }
      lastDownTap = now;
      downWasUp   = false;
    }
  }
  if (!bD) downWasUp = true;

  // ── Auto-repeat (DAS) lateral movement ───────────────
  // On first press move immediately. After DAS_DELAY,
  // repeat every DAS_REPEAT ms while held.
  if (bL && !bR) {
    if (!pL) {
      // Fresh press — move immediately and arm DAS
      if (valid(pieceType, pieceRot, pieceRow, pieceCol - 1)) {
        pieceCol--;
        beep(210, 18);
      }
      dasLeft  = true;
      dasRight = false;
      dasTimer = now;
      dasRepeat = now;
    } else if (dasLeft && now - dasTimer >= DAS_DELAY) {
      // DAS active — repeat at DAS_REPEAT interval
      if (now - dasRepeat >= DAS_REPEAT) {
        dasRepeat = now;
        if (valid(pieceType, pieceRot, pieceRow, pieceCol - 1)) {
          pieceCol--;
          beep(210, 12);
        }
      }
    }
  } else {
    dasLeft = false;
  }

  if (bR && !bL) {
    if (!pR) {
      if (valid(pieceType, pieceRot, pieceRow, pieceCol + 1)) {
        pieceCol++;
        beep(210, 18);
      }
      dasRight = true;
      dasLeft  = false;
      dasTimer = now;
      dasRepeat = now;
    } else if (dasRight && now - dasTimer >= DAS_DELAY) {
      if (now - dasRepeat >= DAS_REPEAT) {
        dasRepeat = now;
        if (valid(pieceType, pieceRot, pieceRow, pieceCol + 1)) {
          pieceCol++;
          beep(210, 12);
        }
      }
    }
  } else {
    dasRight = false;
  }

  // ── Rotation with wall-kick ───────────────────────────
  if (bRot && !pRot) {
    int8_t nr = (pieceRot + 1) % 4;
    if      (valid(pieceType, nr, pieceRow, pieceCol))     { pieceRot = nr; beep(440, 22); }
    else if (valid(pieceType, nr, pieceRow, pieceCol + 1)) { pieceRot = nr; pieceCol++; beep(440, 22); }
    else if (valid(pieceType, nr, pieceRow, pieceCol - 1)) { pieceRot = nr; pieceCol--; beep(440, 22); }
    // Reset lock timer on successful rotate while on ground
    if (onGround) lockTimer = now;
  }

  pL = bL; pR = bR; pD = bD; pRot = bRot;

  // ── Gravity & soft drop ───────────────────────────────
  // Soft drop awards 1pt per cell dropped.
  uint32_t eff = bD ? 55UL : fallInterval;

  if (now - lastFall >= eff) {
    lastFall = now;
    if (valid(pieceType, pieceRot, pieceRow + 1, pieceCol)) {
      pieceRow++;
      onGround = false;
      if (bD) {
        beep(155, 12);
      }
    } else {
      // Piece has landed — start or check lock delay
      if (!onGround) {
        onGround  = true;
        lockTimer = now;
      }
      if (now - lockTimer >= LOCK_DELAY) {
        // Lock delay expired — lock the piece
        onGround = false;
        lockPiece();
        applyScore(sweepLines());
        spawn();
      }
    }
  }

  renderFrame();
}