#include <stdio.h>
#include <stdlib.h>
#include <dos.h>
#include <graphics.h>
#include <time.h>
#include <string.h>
#include <math.h>
#include <bios.h>
#define MAX_OBJECTS 250
#define MAX_BREAKS 5
#define MAX_TIMING_POINTS 50
#define MAX_CURVE_POINTS 18 // it is not recommended to go over 20 since you might get an "array size too large" error
#define FPS 4
#define loopDelay 80
#define PRE_SHOW 1000 // ms the objects show up before the hit
#define AUDIO_DELAY_MS 0 // pretty self explaining
#define BUFFER_SIZE 1024
#define CIRCLE_RADIUS 20
#define APPROACH_SCALE 3.0 // starting approach circle size (defeault is 3x of the circle radius)
#define FOLLOW_RADIUS 10 // nothing i guess i had to remove slider follow circles since they were broken and caused sqrt issues
#define FADE_OUT_MS 410 // here you can configure how long after hit circles disappear
#define MAX_LIFE 100 // Max player health
#define LIFE_DRAIN_PER_SEC 1 // how many health units per second are drained
#define LIFE_GAIN_300 10 // hp gain for 30 points
#define LIFE_LOSS_50 2 // hp loss for 5 points
#define LIFE_LOSS_MISS 4 // hp loss for miss
#define HIT_WINDOW_300 130 // ms hit window for 30 points
#define HIT_WINDOW_100 210 // ms hit window for 10 points
#define HIT_WINDOW_50 350 // ms hit window for 5 points
#define MISS_WINDOW 420 // ms hit window for miss
#define DIRECTION_TOLERANCE 0.5 // cosinus of angle for direction mimic (0.5 ~ 60 degrees)
#define DEBOUNCE_MS 50
// Sound Blaster / OPL-3
#define SB_BASE 0x220
#define SB_RESET SB_BASE+0x6
#define SB_READ SB_BASE+0xA
#define SB_WRITE SB_BASE+0xC
#define SB_STATUS SB_BASE+0xE
#define DMA_CHANNEL 1
#define DMA_ADDR 0x02
#define DMA_COUNT 0x03
#define DMA_MASK 0x0A
#define DMA_MODE 0x0B
#define DMA_FF 0x0C
#define DMA_PAGE 0x83 // For channel 1
#define PIC_MASK 0x21
#define IRQ 5
#define IRQ_VEC (0x08 + IRQ)
// Typy struktur
typedef struct {
    long start, end;
} Break;
typedef struct {
    long time;
    double beatLength;
    int inherited; // 0 for uninherited, 1 for inherited
} TimingPoint;
typedef struct {
    int x, y;
    long time;
    int type; // 1=circle, 2=slider
    int newCombo; // 1 pokud new combo
    int hit; // 0 = nehitnuto, 1 = hitnuto
    // Pro slider:
    char curveType; // 'L' line, 'P' perfect, 'B' bezier, 'C' catmull
    int curvePoints[MAX_CURVE_POINTS][2];
    int curvePointCount;
    double sliderLength;
    int repeats;
} HitObject;
HitObject objects[MAX_OBJECTS];
Break breaks[MAX_BREAKS];
TimingPoint timingPoints[MAX_TIMING_POINTS];
double lastPositiveLenght = 500.0;
int objectCount = 0;
int breakCount = 0;
int timingCount = 0;
long audioLeadIn = 0;
int epilepsyWarning = 0;
double sliderMultiplier = 1.4; // Default, bude prepsano
// Audio DMA + IRQ
void interrupt (*old_irq)(void);
volatile int irq_fired = 0;
volatile int current_buffer = 0;
volatile int end_of_file = 0;
unsigned char *buffers[2];
unsigned int buf_lengths[2];
FILE *wavFile;
long played_bytes = 0;
long last_irq_time = 0;
unsigned int rate = 8000; // Default, bude prepsano z headeru
// Hracske promenne
int playerLife = MAX_LIFE;
long lastLifeDrainTime = 0;
long score = 0;
long count300 = 0;
long count100 = 0;
long count50 = 0;
long countMiss = 0;
// Mys
union REGS regs;
int mouseX = 320, mouseY = 240; // Start uprostred
int prevMouseX = 320, prevMouseY = 240; // Predchozi pro smer
int prevMouseButton = 0;
// Klavesnice debounce
long lastKeyPressTime = 0;
// ----------------------------------------
// Inicializace mysi
int initMouse() {
    regs.x.ax = 0;
    int86(0x33, &regs, &regs);
    return regs.x.ax;
}
// Ziskat pozici mysi
void getMousePos() {
 prevMouseX = mouseX;
 prevMouseY = mouseY;
 regs.x.ax = 3;
 int86(0x33, &regs, &regs);
 mouseX = regs.x.cx;
 mouseY = regs.x.dx;
}
// Check kliknuti mysi (leve tlacitko)
int mouseClicked() {
    regs.x.ax = 3;
    int86(0x33, &regs, &regs);
    return regs.x.bx & 1;
}
// ----------------------------------------
// timing helper
long getTimeMS() {
    return clock() * 1000L / CLOCKS_PER_SEC;
}
// ----------------------------------------
// Parsovat curve points z stringu jako "P|167:333|123:311"
void parseCurvePoints(char *curveStr, char *type, int points[][2], int *count) {
    char *token;
    *type = curveStr[0];
    token = strtok(curveStr + 2, "|"); // Preskocit "P|"
    *count = 0;
    while (token && *count < MAX_CURVE_POINTS) {
sscanf(token, "%d:%d", &points[*count][0], &points[*count][1]);
(*count)++;
token = strtok(NULL, "|");
    }
}
// ----------------------------------------
// Load osu map, spinnery ignorujeme
void loadBeatmap(const char* filename) {
    FILE *f;
    char line[512];
    int inGeneral;
    int inDifficulty;
    int inEvents;
    int inTimingPoints;
    int inHitObjects;
    int x, y, type, hitsound;
    long t;
    char curve[256];
    double length;
    int repeats;
    int res;
    double bl;
    f = fopen(filename, "r");
    if (!f) {
printf("Unable to open file %s\n", filename);
exit(1);
    }
    objectCount = 0;
    breakCount = 0;
    timingCount = 0;
    audioLeadIn = 0;
    epilepsyWarning = 0;
    sliderMultiplier = 1.4;
    inGeneral = 0;
    inDifficulty = 0;
    inEvents = 0;
    inTimingPoints = 0;
    inHitObjects = 0;
    while (fgets(line, sizeof(line), f)) {
if (line[0] == '[') {
    if (strncmp(line, "[General]", 9) == 0) inGeneral = 1; else inGeneral = 0;
    if (strncmp(line, "[Difficulty]", 12) == 0) inDifficulty = 1; else inDifficulty = 0;
    if (strncmp(line, "[Events]", 8) == 0) inEvents = 1; else inEvents = 0;
    if (strncmp(line, "[TimingPoints]", 14) == 0) inTimingPoints = 1; else inTimingPoints = 0;
    if (strncmp(line, "[HitObjects]", 12) == 0) inHitObjects = 1; else inHitObjects = 0;
    continue;
}
if (inGeneral) {
    if (strncmp(line, "AudioLeadIn:", 12) == 0) {
sscanf(line + 12, "%ld", &audioLeadIn);
    } else if (strncmp(line, "EpilepsyWarning:", 16) == 0) {
sscanf(line + 16, "%d", &epilepsyWarning);
    }
    continue;
}
if (inDifficulty) {
    if (strncmp(line, "SliderMultiplier:", 17) == 0) {
sscanf(line + 17, "%lf", &sliderMultiplier);
    }
    continue;
}
if (inEvents && line[0] == '2') {
    long s, e;
    res = sscanf(line, "2,%ld,%ld", &s, &e);
    if (res == 2 && breakCount < MAX_BREAKS) {
breaks[breakCount].start = s;
breaks[breakCount].end = e;
breakCount++;
    }
    continue;
}
if (inTimingPoints) {
    res = sscanf(line, "%ld,%lf", &t, &bl);
    if (res == 2 && timingCount < MAX_TIMING_POINTS) {
timingPoints[timingCount].time = t;
timingPoints[timingCount].beatLength = bl;
timingPoints[timingCount].inherited = (bl < 0);
timingCount++;
    }
    continue;
}
if (!inHitObjects) continue;
if (objectCount >= MAX_OBJECTS) break;
// Format: x,y,time,type,hitsound,[slider specifics]
if (strstr(line, "|")) { // Slider
    res = sscanf(line, "%d,%d,%ld,%d,%d,%[^,],%lf,%d", &x, &y, &t, &type, &hitsound, curve, &length, &repeats);
    if (res >= 7) {
if (type & 8) continue; // spinner ignorujeme
objects[objectCount].x = x;
objects[objectCount].y = y;
objects[objectCount].time = t;
objects[objectCount].type = 2;
objects[objectCount].newCombo = (type & 4) ? 1 : 0;
objects[objectCount].hit = 0;
parseCurvePoints(curve, &objects[objectCount].curveType, objects[objectCount].curvePoints, &objects[objectCount].curvePointCount);
objects[objectCount].sliderLength = length;
objects[objectCount].repeats = repeats;
objectCount++;
    }
} else { // Circle
    res = sscanf(line, "%d,%d,%ld,%d", &x, &y, &t, &type);
    if (res >= 4) {
if (type & 8) continue; // spinner ignorujeme
objects[objectCount].x = x;
objects[objectCount].y = y;
objects[objectCount].time = t;
objects[objectCount].type = 1;
objects[objectCount].newCombo = (type & 4) ? 1 : 0;
objects[objectCount].hit = 0;
objectCount++;
    }
}
    }
    fclose(f);
}
// ----------------------------------------
// Vypocet duration pro slider
double calculateSliderDuration(long sliderTime, double sliderLength, int repeats) {
    double baseBeatLength = 600.0; // Default
    double sv = 1.0;
    int i;
    double effectiveSV;
    double beats;
    double durationOne;
    // Najit posledni uninherited TP <= sliderTime
    for (i = timingCount - 1; i >= 0; i--) {
if (timingPoints[i].time <= sliderTime && !timingPoints[i].inherited) {
    baseBeatLength = timingPoints[i].beatLength;
    break;
}
    }
    /* Najit posledni TP <= sliderTime pro SV */
    for (i = timingCount - 1; i >= 0; i--) {
if (timingPoints[i].time <= sliderTime) {
    if (timingPoints[i].inherited) {
if (timingPoints[i].beatLength != 0.0) {
    sv = -100.0 / timingPoints[i].beatLength;
} else {
    sv = 1.0;
}
    } else {
sv = 1.0;
    }
    break;
}
    }
    /* Zabrana deleni nulou - zachovej rozumny default pri degenerovanych mapach */
    if (baseBeatLength <= 0.0) baseBeatLength = 500.0;
    effectiveSV = sliderMultiplier * sv;
    if (effectiveSV <= 0.0) effectiveSV = 1.0;
    beats = sliderLength / (100.0 * effectiveSV);
    durationOne = beats * baseBeatLength;
    if (repeats < 1) repeats = 1;
    return durationOne * repeats;
}
// ----------------------------------------
// Kresleni jednoduche krivky - pro 'L' line, pro ostatni aproximace line segments
void drawSliderPath(int x, int y, int points[][2], int count, char curveType) {
    int i;
    setcolor(WHITE);
    line(x, y, points[0][0], points[0][1]);
    for (i = 0; i < count - 1; i++) {
line(points[i][0], points[i][1], points[i+1][0], points[i+1][1]);
    }
    // Pro slozitejsi krivky (P,B,C) by slo aproximovat vic, ale pro jednoduchost line segments mezi points
}
// ----------------------------------------
// Vypocet pozice follow circle podle progress (0-1 pro jeden pass)
void getFollowPosition(long x, long y, long curvePoints[][2], long curveCount, double progress, long *targetX, long *targetY) {
    long pathPoints[MAX_CURVE_POINTS + 1][2];
    double segLengths[MAX_CURVE_POINTS];
    double totalLength = 0.0;
    int i;
    double cum = 0.0;
    int pathCount;
    long dx, dy;
    double targetDist;
    double frac;
    /* Vytvorit path: start + curve points */
    pathPoints[0][0] = x;
    pathPoints[0][1] = y;
    for (i = 0; i < curveCount; i++) {
pathPoints[i + 1][0] = curvePoints[i][0];
pathPoints[i + 1][1] = curvePoints[i][1];
    }
    pathCount = (int)curveCount + 1;
    /* Vypocitat delky segmentu - pouzit long kvuli 16b int overflow pri dx*dx */
    for (i = 0; i < pathCount - 1; i++) {
dx = pathPoints[i + 1][0] - pathPoints[i][0];
dy = pathPoints[i + 1][1] - pathPoints[i][1];
segLengths[i] = sqrt((double)(dx * dx + dy * dy));
totalLength += segLengths[i];
    }
    if (totalLength <= 0.0) {
*targetX = x;
*targetY = y;
return;
    }
    /* Target distance podle progress */
    targetDist = progress * totalLength;
    for (i = 0; i < pathCount - 1; i++) {
if (cum + segLengths[i] >= targetDist) {
    if (segLengths[i] <= 0.0) {
*targetX = pathPoints[i][0];
*targetY = pathPoints[i][1];
    } else {
frac = (targetDist - cum) / segLengths[i];
*targetX = pathPoints[i][0] + (long)(frac * (pathPoints[i + 1][0] - pathPoints[i][0]));
*targetY = pathPoints[i][1] + (long)(frac * (pathPoints[i + 1][1] - pathPoints[i][1]));
    }
    return;
}
cum += segLengths[i];
    }
    /* Pokud progress=1, posledni bod */
    *targetX = pathPoints[pathCount - 1][0];
    *targetY = pathPoints[pathCount - 1][1];
}
void interrupt irq_handler(void) {
    inportb(SB_STATUS); // Ack DSP interrupt
    outportb(0x20, 0x20); // EOI to PIC
    irq_fired = 1;
}
int sb_reset(void) {
    int i;
    outportb(SB_RESET, 1);
    delay(10);
    outportb(SB_RESET, 0);
    delay(10);
    for (i = 0; i < 1000; i++) {
if (inportb(SB_STATUS) & 0x80) {
    if (inportb(SB_READ) == 0xAA) {
return 1;
    }
}
    }
    return 0;
}
void sb_write_dsp(unsigned char cmd) {
    while (inportb(SB_WRITE) & 0x80);
    outportb(SB_WRITE, cmd);
}
unsigned char sb_read_dsp(void) {
    while (!(inportb(SB_STATUS) & 0x80));
    return inportb(SB_READ);
}
void setup_dma(unsigned char *buffer, unsigned int length) {
    unsigned seg;
    unsigned off;
    unsigned long phys;
    unsigned int page, addr;
    seg = _DS;
    off = (unsigned)buffer;
    phys = ((unsigned long)seg << 4) + off;
    page = phys >> 16;
    addr = phys & 0xFFFF;
    outportb(DMA_MASK, DMA_CHANNEL | 4); // Mask channel
    outportb(DMA_FF, 0); // Clear flip-flop
    outportb(DMA_MODE, 0x48 + DMA_CHANNEL); // Single mode, playback
    outportb(DMA_ADDR, addr & 0xFF);
    outportb(DMA_ADDR, addr >> 8);
    outportb(DMA_COUNT, (length-1) & 0xFF);
    outportb(DMA_COUNT, (length-1) >> 8);
    outportb(DMA_PAGE, page);
    outportb(DMA_MASK, DMA_CHANNEL); // Unmask
}
void sb_set_rate(unsigned int sampleRate) {
    unsigned char tc;
    if (sampleRate == 0) sampleRate = 8000;
    /* 1000000L zajisti long aritmetiku pod Turbo C (int=16b) */
    tc = (unsigned char)(256L - (1000000L / (long)sampleRate));
    sb_write_dsp(0x40); /* Set time constant */
    sb_write_dsp(tc);
}
void sb_play(unsigned int length) {
    sb_write_dsp(0x14); // 8-bit single cycle DMA
    sb_write_dsp((length-1) & 0xFF);
    sb_write_dsp((length-1) >> 8);
}
unsigned int fill_buffer(unsigned char *buffer) {
    unsigned int bytes_read = fread(buffer, 1, BUFFER_SIZE, wavFile);
    if (bytes_read < BUFFER_SIZE) {
end_of_file = 1;
    }
    return bytes_read;
}
// ----------------------------------------
// Check if point is inside circle
int isInsideCircle(int cx, int cy, int r, int px, int py) {
    /* long aritmetika - pod Turbo C je int 16b a dx*dx snadno pretece */
    long dx = (long)px - (long)cx;
    long dy = (long)py - (long)cy;
    long lr = (long)r;
    return (dx * dx + dy * dy) <= (lr * lr);
}
// ----------------------------------------
// Check naznaceni smeru pro slider
int checkDirection(long sliderX, long sliderY, long nextX, long nextY, long mouseDX, long mouseDY) {
    long sliderDX;
    long sliderDY;
    double sliderMag;
    double mouseMag;
    double dot;
    sliderDX = nextX - sliderX;
    sliderDY = nextY - sliderY;
    sliderMag = sqrt((double)(sliderDX * sliderDX + sliderDY * sliderDY));
    mouseMag = sqrt((double)(mouseDX * mouseDX + mouseDY * mouseDY));
    if (sliderMag == 0.0 || mouseMag == 0.0) return 0;
    dot = ((double)sliderDX * mouseDX + (double)sliderDY * mouseDY) / (sliderMag * mouseMag);
    return dot > DIRECTION_TOLERANCE;
}
// ----------------------------------------
// Kresleni life baru
void drawLifeBar(int life) {
    setcolor(GREEN);
    rectangle(10, 30, 10 + (life * 2), 40); // Bar sirka podle zivotu
    setfillstyle(SOLID_FILL, GREEN);
    bar(10, 30, 10 + (life * 2), 40);
}
// ----------------------------------------
// Koncovy screen
void showEndScreen() {
    long totalHits;
    double accuracy;
    char buf[64];
    totalHits = count300 + count100 + count50 + countMiss;
    accuracy = 0.0;
    if (totalHits > 0) {
/* Pozn.: 100L kvuli long aritmetice; 300.0 vynuti double deleni */
accuracy = 100.0 * (count300 * 300L + count100 * 100L + count50 * 50L) / (totalHits * 300.0);
    }
    cleardevice();
    setcolor(WHITE);
    outtextxy(10, 10, "Finish - statistics:");
    sprintf(buf, "30: %ld", count300);
    outtextxy(10, 50, buf);
    sprintf(buf, "10: %ld", count100);
    outtextxy(10, 70, buf);
    sprintf(buf, "5: %ld", count50);
    outtextxy(10, 90, buf);
    sprintf(buf, "misses: %ld", countMiss);
    outtextxy(10, 110, buf);
    sprintf(buf, "total score: %ld", score);
    outtextxy(10, 130, buf);
    sprintf(buf, "accuracy: %.2f%%", accuracy);
    outtextxy(10, 150, buf);
    outtextxy(10, 170, "press any key to continue...");
    getch();
}
// ----------------------------------------
// main
int main() {
    int gd = DETECT, gm;
    long now, time_since_irq, additional_bytes, total_bytes;
    int i, active, j, inBreak;
    char buf[64], numBuf[3];
    unsigned char *alloc_buf1, *alloc_buf2;
    unsigned seg, off;
    unsigned long phys;
    int done = 0;
    unsigned long sample_rate;
    unsigned char rate_bytes[4];
    int currentCombo;
    long appearTime;
    long disappearTime;
    float progress;
    int approachR;
    double totalDuration;
    double durationOne;
    double timePassed;
    double pass;
    float localProgress;
    int endX;
    int endY;
    int currentMouseButton;
    int mousePress;
    int keyPress;
    long hitTime;
    int mouseDX, mouseDY;
    int nextX, nextY;
    int directionOK;
    int isHit;
    int baseScore;
    loadBeatmap("map.osu");
    printf("Map loaded: %d objects, %d breaks, %d timings, AudioLeadIn: %ld ms, SliderMultiplier: %.2f\n", objectCount, breakCount, timingCount, audioLeadIn, sliderMultiplier);
    if (!initMouse()) {
printf("Myš nenalezena\n");
return 1;
    }
    initgraph(&gd, &gm, "C:\\TC\\BGI");
    if (graphresult() != grOk) {
printf("Graphics error\n");
return 1;
    }
    // Zobrazeni epilepsy warning pokud je nastaveno
    if (epilepsyWarning) {
cleardevice();
outtextxy(100, 200, "Epilepsy Warning! Press any key to continue...");
getch();
cleardevice();
    }
    // otevreni WAV souboru
    wavFile = fopen("audio.wav", "rb");
    if (!wavFile) {
printf("Nelze otevrit audio.wav\n");
closegraph();
exit(1);
    }
    // Cteni sample rate z WAV headeru (bytes 24-27, little endian)
    fseek(wavFile, 24, SEEK_SET);
    fread(rate_bytes, 1, 4, wavFile);
    /* Cast na unsigned long pred shiftem - pod Turbo C je int 16b a shift 16/24 by prisel o vyssi byty */
    sample_rate = (unsigned long)rate_bytes[0]
                | ((unsigned long)rate_bytes[1] << 8)
                | ((unsigned long)rate_bytes[2] << 16)
                | ((unsigned long)rate_bytes[3] << 24);
    if (sample_rate > 0 && sample_rate <= 44100) {
rate = (unsigned int)sample_rate;
    } else {
printf("Nepodporovana sample rate %lu, pouzivam default 8000\n", sample_rate);
    }
    fseek(wavFile, 44, SEEK_SET); // preskocit header 44B
    // Allocate buffers
    alloc_buf1 = (unsigned char *)malloc(BUFFER_SIZE + 65536);
    alloc_buf2 = (unsigned char *)malloc(BUFFER_SIZE + 65536);
    if (!alloc_buf1 || !alloc_buf2) {
printf("No memory\n");
fclose(wavFile);
closegraph();
return 1;
    }
    /* Align buffers - pouzit unsigned long pro kontrolu, jinak (off+BUFFER_SIZE) pretece 16b unsigned */
    {
unsigned long loff;
buffers[0] = alloc_buf1;
seg = _DS;
off = (unsigned)buffers[0];
phys = ((unsigned long)seg << 4) + off;
loff = phys & 0xFFFFUL;
if (loff + BUFFER_SIZE > 65536UL) buffers[0] += (unsigned)(65536UL - loff);
buffers[1] = alloc_buf2;
off = (unsigned)buffers[1];
phys = ((unsigned long)seg << 4) + off;
loff = phys & 0xFFFFUL;
if (loff + BUFFER_SIZE > 65536UL) buffers[1] += (unsigned)(65536UL - loff);
    }
    // Fill initial buffers
    buf_lengths[0] = fill_buffer(buffers[0]);
    buf_lengths[1] = fill_buffer(buffers[1]);
    // Reset SB
    if (!sb_reset()) {
printf("SB reset failed\n");
free(alloc_buf1);
free(alloc_buf2);
fclose(wavFile);
closegraph();
return 1;
    }
    // Turn on speaker
    sb_write_dsp(0xD1);
    // Hook IRQ
    old_irq = getvect(IRQ_VEC);
    setvect(IRQ_VEC, irq_handler);
    outportb(PIC_MASK, inportb(PIC_MASK) & ~(1 << IRQ));
    // Set rate
    sb_set_rate(rate);
    /* Start timer for graphics */
    lastLifeDrainTime = getTimeMS();
    lastKeyPressTime = lastLifeDrainTime;
    prevMouseButton = 0;
    // Delay pro AudioLeadIn
    delay(audioLeadIn);
    // Nyni start audia
    last_irq_time = getTimeMS();
    played_bytes = 0;
    current_buffer = 0;
    end_of_file = 0;
    irq_fired = 0;
    setup_dma(buffers[current_buffer], buf_lengths[current_buffer]);
    sb_play(buf_lengths[current_buffer]);
    // hlavni smycka
    while (!done && playerLife > 0) {
// Vypocet casu
time_since_irq = getTimeMS() - last_irq_time;
additional_bytes = (time_since_irq * (long)rate) / 1000;
total_bytes = played_bytes + additional_bytes;
now = (total_bytes * 1000L) / rate - AUDIO_DELAY_MS;
cleardevice();
// Check if in break
inBreak = 0;
for (j = 0; j < breakCount; j++) {
    if (now >= breaks[j].start && now < breaks[j].end) {
inBreak = 1;
break;
    }
}
// Life drain za cas
if (!inBreak) {
    if (getTimeMS() - lastLifeDrainTime >= 1000) {
        playerLife -= LIFE_DRAIN_PER_SEC;
        lastLifeDrainTime = getTimeMS();
        if (playerLife < 0) playerLife = 0;
    }
}
// Kreslit life bar
drawLifeBar(playerLife);
// Ziskat mys
getMousePos();
// Check input
currentMouseButton = mouseClicked();
mousePress = currentMouseButton && !prevMouseButton;
prevMouseButton = currentMouseButton;
keyPress = 0;
if (kbhit()) {
    char c = getch();
    if (c == 'x' || c == 'X') {
        if (getTimeMS() - lastKeyPressTime > DEBOUNCE_MS) {
            keyPress = 1;
            lastKeyPressTime = getTimeMS();
        }
    }
}
hitTime = now; // Cas kliku
mouseDX = mouseX - prevMouseX;
mouseDY = mouseY - prevMouseY;
if (inBreak) {
    outtextxy(200, 200, "Break");
} else {
    active = 0;
    currentCombo = 1;
    for (i = 0; i < objectCount; i++) {
appearTime = objects[i].time - PRE_SHOW;
disappearTime = objects[i].time + MISS_WINDOW + FADE_OUT_MS; // Prodlozit pro miss check
if (objects[i].type == 2) {
    int rep = objects[i].repeats > 0 ? objects[i].repeats : 1;
    totalDuration = calculateSliderDuration(objects[i].time, objects[i].sliderLength, rep);
    /* durationOne = delka jednoho pruchodu slideru; drive bylo neinicializovane a pouzivane pro deleni */
    durationOne = totalDuration / (double)rep;
    if (durationOne <= 0.0) durationOne = 1.0;
    disappearTime = objects[i].time + (long)totalDuration + FADE_OUT_MS;
}
if (now < appearTime || now > disappearTime) continue;
if (objects[i].newCombo) currentCombo = 1;
else currentCombo = (currentCombo % 20) + 1;
if (objects[i].hit) continue; // Uz hitnuto
// Check miss
if (now > objects[i].time + MISS_WINDOW) {
    countMiss++;
    playerLife -= LIFE_LOSS_MISS;
    if (playerLife < 0) playerLife = 0;
    objects[i].hit = 1;
    setcolor(RED);
    outtextxy(objects[i].x - 20, objects[i].y + 30, "chyba");
    continue;
}
// Check hit
isHit = 0;
baseScore = 0;
if (mousePress || keyPress) {
    if (isInsideCircle(objects[i].x, objects[i].y, CIRCLE_RADIUS, mouseX, mouseY)) {
long deviation = labs(hitTime - objects[i].time);
if (deviation < HIT_WINDOW_300) {
    baseScore = 30;
} else if (deviation < HIT_WINDOW_100) {
    baseScore = 10;
} else if (deviation < HIT_WINDOW_50) {
    baseScore = 5;
} else {
    baseScore = 0;
}
if (baseScore > 0) {
    if (objects[i].type == 2) { // Pro slider check smer
if (objects[i].curvePointCount > 0) {
    nextX = objects[i].curvePoints[0][0];
    nextY = objects[i].curvePoints[0][1];
} else {
    nextX = objects[i].x + 10; // Default smer
    nextY = objects[i].y;
}
directionOK = checkDirection(objects[i].x, objects[i].y, nextX, nextY, mouseDX, mouseDY);
if (baseScore == 30 && !directionOK) {
    baseScore = 10;
}
    }
    score += baseScore;
    if (baseScore == 30) {
count300++;
playerLife += LIFE_GAIN_300;
if (playerLife > MAX_LIFE) playerLife = MAX_LIFE;
    } else if (baseScore == 10) {
count100++;
    } else if (baseScore == 5) {
count50++;
playerLife -= LIFE_LOSS_50;
if (playerLife < 0) playerLife = 0;
    }
    objects[i].hit = 1;
    isHit = 1;
} else {
    countMiss++;
    playerLife -= LIFE_LOSS_MISS;
    if (playerLife < 0) playerLife = 0;
    setcolor(RED);
    outtextxy(objects[i].x - 20, objects[i].y + 30, "chyba");
    objects[i].hit = 1;
    isHit = 1;
}
    }
}
if (isHit) continue;
// Kreslit objekt
if (objects[i].type == 1) { // Circle
    // Approach circle pokud pred hitem
    if (now < objects[i].time) {
progress = (objects[i].time - now) / (float)PRE_SHOW;
approachR = CIRCLE_RADIUS + (int)((APPROACH_SCALE - 1.0) * CIRCLE_RADIUS * progress);
setcolor(LIGHTGRAY);
circle(objects[i].x, objects[i].y, approachR);
    }
    // Kruh
    setcolor(GREEN);
    circle(objects[i].x, objects[i].y, CIRCLE_RADIUS);
    // Cislo
    sprintf(numBuf, "%d", currentCombo);
    outtextxy(objects[i].x - 5, objects[i].y - 5, numBuf);
} else if (objects[i].type == 2) { // Slider
    // Kreslit path
    drawSliderPath(objects[i].x, objects[i].y, objects[i].curvePoints, objects[i].curvePointCount, objects[i].curveType);
    // Start kruh s cislem
    setcolor(GREEN);
    circle(objects[i].x, objects[i].y, CIRCLE_RADIUS);
    sprintf(numBuf, "%d", currentCombo);
    outtextxy(objects[i].x - 5, objects[i].y - 5, numBuf);
    // End kruh - modry pokud repeats >1
    endX = objects[i].curvePoints[objects[i].curvePointCount-1][0];
    endY = objects[i].curvePoints[objects[i].curvePointCount-1][1];
    setcolor((objects[i].repeats > 1) ? BLUE : GREEN);
    circle(endX, endY, CIRCLE_RADIUS);
    // Approach na startu pred hitem
    if (now < objects[i].time) {
progress = (objects[i].time - now) / (float)PRE_SHOW;
approachR = CIRCLE_RADIUS + (int)((APPROACH_SCALE - 1.0) * CIRCLE_RADIUS * progress);
setcolor(LIGHTGRAY);
circle(objects[i].x, objects[i].y, approachR);
    }
    // Follow circle po hitu, ale jen do konce totalDuration
    if (now >= objects[i].time) {
timePassed = now - objects[i].time;
if (timePassed > totalDuration) timePassed = totalDuration;
pass = timePassed / durationOne;
localProgress = (float)(pass - floor(pass));
if (((int)floor(pass)) % 2 == 1) localProgress = 1.0f - localProgress;
    }
}
active++;
    }
}
        // Kreslit kurzor
        setcolor(YELLOW);
        circle(mouseX, mouseY, 5);
        sprintf(buf, "t=%ld ms, active=%d", now, active);
        outtextxy(10, 10, buf);
        // Handle audio IRQ
        if (irq_fired) {
            irq_fired = 0;
            played_bytes += buf_lengths[current_buffer];
            last_irq_time = getTimeMS();
            current_buffer = 1 - current_buffer;
            if (!end_of_file) {
                buf_lengths[1 - current_buffer] = fill_buffer(buffers[1 - current_buffer]);
            } else {
                buf_lengths[1 - current_buffer] = 0;
            }
            if (buf_lengths[current_buffer] > 0) {
                setup_dma(buffers[current_buffer], buf_lengths[current_buffer]);
                sb_play(buf_lengths[current_buffer]);
            } else {
                end_of_file = 1;
            }
        }
        // Check if all objects done or audio ended
        if ((objectCount > 0 && now > objects[objectCount-1].time + 1000) || end_of_file) {
            done = 1;
        }
        // FPS limit
delay(loopDelay / FPS);
    }
    // Koncovy screen
    showEndScreen();
    // Cleanup
    outportb(PIC_MASK, inportb(PIC_MASK) | (1 << IRQ));
    setvect(IRQ_VEC, old_irq);
    sb_write_dsp(0xD3); // turn off speaker
    free(alloc_buf1);
    free(alloc_buf2);
    fclose(wavFile);
    closegraph();
    return 0;
}