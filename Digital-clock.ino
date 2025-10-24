// ===== UNO + ILI9488 Shield: Clock + Stopwatch + SET (Optimized) =====
// Key optimizations:
// - Removed Serial debugging
// - Optimized String usage
// - Reduced function call overhead
// - Simplified formatting functions

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <MCUFRIEND_kbv.h>
#include <TouchScreen.h>
#include <RTClib.h>
#include <EEPROM.h>

MCUFRIEND_kbv tft;

// ---------- Touch wiring ----------
#define YP A3
#define XM A2
#define YM 9
#define XP 8
TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300);

// ---------- Touch calibration ----------
int16_t TS_MINX = 162, TS_MAXX = 963, TS_MINY = 158, TS_MAXY = 912;
#define TOUCH_SWAP_XY  1
#define TOUCH_FLIP_X   1
#define TOUCH_FLIP_Y   1

// ---------- Screen ----------
const int16_t TFT_W = 480, TFT_H = 320;

// ---------- RTC ----------
RTC_DS3231 rtc;
bool rtcOK = false;

// ---------- Colors ----------
#define RGB565(r,g,b) ( ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b) >> 3) )
const uint16_t COL_BG   = 0x0000;
const uint16_t COL_FG   = 0xFFFF;
const uint16_t COL_DIM  = 0xA514;
const uint16_t COL_ACC  = 0xFDE0;
const uint16_t COL_BTN  = 0x2124;
const uint16_t COL_BRDR = 0x6B4D;
const uint16_t COL_HIT  = 0x3E78;

// ---------- EEPROM ----------
const int EEPROM_ADDR_FMT_12_24 = 0;
const int EEPROM_ADDR_DATEFMT   = 1;

// ---------- State ----------
bool is24h = true;
uint8_t dateFmt = 0;
uint32_t lastTickMs = 0;

// ---------- Software Clock ----------
struct SoftEpoch { uint16_t year=2025; uint8_t month=1, day=1, hour=12, minute=0, second=0; } soft;
uint32_t baseMillis = 0;

DateTime softNow(){
  uint32_t s=(millis()-baseMillis)/1000UL;
  DateTime b(soft.year,soft.month,soft.day,soft.hour,soft.minute,soft.second);
  return b+TimeSpan(s);
}
void softAdjust(const DateTime& dt){
  soft.year=dt.year(); soft.month=dt.month(); soft.day=dt.day();
  soft.hour=dt.hour(); soft.minute=dt.minute(); soft.second=dt.second();
  baseMillis=millis();
}

// ---------- Modes ----------
enum UIMode { MODE_CLOCK, MODE_SET, MODE_STOPWATCH };
UIMode uiMode = MODE_CLOCK;

// Cached last-drawn strings for clock lines (minimize SRAM on UNO)
char g_lastTime[20] = "";  // matches timeStr size
char g_lastDate[32] = "";  // matches dateStr size

// Forward declarations for helpers used in frame drawing
void invalidateClockText();
void drawClockNow();
void drawTimeSmooth(const char* timeStr);
// Stopwatch smooth-draw helpers
void invalidateStopwatchText();
void drawStopwatchTimeSmooth(const char* txt);

// ---------- Bottom Buttons ----------
const int BAR_Y = 270, BAR_H = 40;
const int CLK_BTN_W = 140;
const int BTN_FMT_X = 10, BTN_FMT_Y = BAR_Y;
const int BTN_SW_X  = 170, BTN_SW_Y = BAR_Y;
const int BTN_SET_X = 330, BTN_SET_Y = BAR_Y;

// ---------- Stopwatch Bottom Buttons ----------
const int SW_BTN_W = 110, SW_BTN_H = BAR_H;
const int SW_GAP = 10;
const int SW_Y = BAR_Y;
const int SW_START_X = SW_GAP;
const int SW_LAP_X   = 130;
const int SW_RESET_X = 250;
const int SW_BACK_X  = 370;

// ---------- Stopwatch State ----------
bool swRunning=false;
uint32_t swStartMs=0, swAccumMs=0, swLastDraw=0, swLap[4]={0};
uint8_t swLapCount=0;
// Cached stopwatch last text
char g_lastSw[16] = "";

// ---------- Utilities (optimized) ----------

const char* dowName(uint8_t d){ 
  static char Sun[]="Sun", Mon[]="Mon", Tue[]="Tue", Wed[]="Wed", Thu[]="Thu", Fri[]="Fri", Sat[]="Sat";
  static char* n[7] = {Sun,Mon,Tue,Wed,Thu,Fri,Sat}; 
  return n[d%7]; 
}

const char* monName(uint8_t m){ 
  static char Jan[]="Jan", Feb[]="Feb", Mar[]="Mar", Apr[]="Apr", May[]="May", Jun[]="Jun", Jul[]="Jul", Aug[]="Aug", Sep[]="Sep", Oct_[]="Oct", Nov[]="Nov", Dec[]="Dec";
  static char* n[12] = {Jan,Feb,Mar,Apr,May,Jun,Jul,Aug,Sep,Oct_,Nov,Dec}; 
  return n[m-1]; 
}

// Use char buffers instead of String objects
void formatTime(char* buf, uint8_t h, uint8_t m, uint8_t s, bool tf){
  if(tf) {
    sprintf(buf,"%02u:%02u:%02u",h,m,s);
  } else {
    uint8_t hh=h%12; if(hh==0) hh=12;
    sprintf(buf,"%2u:%02u:%02u %s",hh,m,s,h<12?"AM":"PM");
  }
}

void formatDate(char* buf, uint8_t dow, uint8_t dd, uint8_t mm, uint16_t yyyy, uint8_t fmt){
  switch(fmt){
    case 0: sprintf(buf,"%s, %02u %s %04u",dowName(dow),dd,monName(mm),yyyy); break;
    case 1: sprintf(buf,"%02u-%02u-%04u",dd,mm,yyyy); break;
    case 2: sprintf(buf,"%02u/%02u/%04u",mm,dd,yyyy); break;
    case 3: sprintf(buf,"%04u-%02u-%02u",yyyy,mm,dd); break;
    case 4: sprintf(buf,"%02u %s %04u",dd,monName(mm),yyyy); break;
    default: sprintf(buf,"%s, %02u %s %04u",dowName(dow),dd,monName(mm),yyyy); break;
  }
}

// ---------- UI helpers ----------
void drawButton(int x,int y,int w,int h,const char* label,uint16_t fg,uint16_t bg){
  tft.fillRect(x,y,w,h,bg); 
  tft.drawRect(x,y,w,h,COL_BRDR);
  tft.setTextColor(fg,bg); 
  tft.setTextSize(2);
  int16_t tx=x+(w-strlen(label)*12)/2, ty=y+(h-16)/2;
  if(tx < x+2) tx = x+2;
  tft.setCursor(tx,ty); 
  tft.print(label);
}

void centerText(const char* txt, int y, uint8_t size, uint16_t color, uint16_t bg, bool clear){
  int w=strlen(txt)*6*size, x=(TFT_W-w)/2;
  if(clear){
    int h=8*size+8;
    int yT=y-h+4; 
    if(yT<0)yT=0; 
    tft.fillRect(0,yT,TFT_W,h+8,bg);
  }
  tft.setTextColor(color,bg); 
  tft.setTextSize(size); 
  tft.setCursor(x,y-(8*size)); 
  tft.print(txt);
}

bool inRect(int x,int y,int rx,int ry,int rw,int rh){
  return(x>=rx&&x<rx+rw&&y>=ry&&y<ry+rh);
}

// ---------- Touch ----------
bool readTouch(int16_t &sx,int16_t &sy){
  TSPoint p=ts.getPoint();
  pinMode(YP,OUTPUT); pinMode(XM,OUTPUT); 
  digitalWrite(YP,HIGH); digitalWrite(XM,HIGH);
  if(p.z<50||p.z>1000)return false;
  int16_t rx=p.x,ry=p.y; 
  if(TOUCH_SWAP_XY){int16_t t=rx;rx=ry;ry=t;}
  int16_t x=map(rx,TS_MINX,TS_MAXX,0,TFT_W), y=map(ry,TS_MINY,TS_MAXY,0,TFT_H);
  if(TOUCH_FLIP_X)x=TFT_W-1-x; 
  if(TOUCH_FLIP_Y)y=TFT_H-1-y;
  sx=constrain(x,0,TFT_W-1); sy=constrain(y,0,TFT_H-1);
  delay(16);
  return true;
}

// ---------- Clock ----------
void drawClockFrame(){
  tft.fillScreen(COL_BG);
  centerText("Digital Clock",42,2,COL_ACC,COL_BG,false);
  centerText("FMT / STOPWATCH / SET",80,2,COL_DIM,COL_BG,false);
  drawButton(BTN_FMT_X,BTN_FMT_Y,CLK_BTN_W,BAR_H,is24h?"FMT:24H":"FMT:12H",COL_FG,COL_BTN);
  drawButton(BTN_SW_X,BTN_SW_Y,CLK_BTN_W,BAR_H,"STOPWATCH",COL_FG,COL_BTN);
  drawButton(BTN_SET_X,BTN_SET_Y,CLK_BTN_W,BAR_H,"SET",COL_FG,COL_BTN);
  
  // Ensure time/date are drawn after a screen clear
  invalidateClockText();
  drawClockNow();
}

void drawClockLines(const char* timeStr, const char* dateStr){
  // Draw time with minimal flicker (only update changed characters)
  drawTimeSmooth(timeStr);

  // Date changes rarely; simple redraw when needed
  if(strcmp(dateStr,g_lastDate)!=0){ 
    centerText(dateStr,220,3,COL_ACC,COL_BG,true); 
    strcpy(g_lastDate,dateStr); 
  }
}

// ---------- Time fetch ----------
void getNow(uint8_t &h,uint8_t &m,uint8_t &s,uint8_t &dd,uint8_t &mm,uint16_t &yyyy,uint8_t &dow){
  DateTime n = rtcOK ? rtc.now() : softNow();
  h=n.hour(); m=n.minute(); s=n.second();
  dd=n.day(); mm=n.month(); yyyy=n.year();
  dow=n.dayOfTheWeek();
}

// Invalidate cached strings so next draw repaints both lines
void invalidateClockText(){
  g_lastTime[0] = 0;
  g_lastDate[0] = 0;
}

// Draw current time+date immediately (used when entering clock screen)
void drawClockNow(){
  uint8_t h,m,s,dd,mm,dow; 
  uint16_t yyyy;
  getNow(h,m,s,dd,mm,yyyy,dow);
  char timeStr[20], dateStr[32];
  formatTime(timeStr, h, m, s, is24h);
  formatDate(dateStr, dow, dd, mm, yyyy, dateFmt);
  drawClockLines(timeStr, dateStr);
}

// Smooth time redraw: updates only characters that changed to minimize flicker
void drawTimeSmooth(const char* timeStr){
  const uint8_t size = 6;            // matches main clock font size
  const uint8_t charW = 6 * size;    // default 5x7 font + 1 spacing
  const uint8_t charH = 8 * size;
  const int yCenter = 160;
  const int yTop = yCenter - charH;  // centerText uses y - 8*size

  tft.setTextColor(COL_FG, COL_BG);
  tft.setTextSize(size);

  int newLen = (int)strlen(timeStr);
  int oldLen = (int)strlen(g_lastTime);
  int w = newLen * charW;
  int x = (TFT_W - w) / 2;

  if (newLen != oldLen) {
    // If layout changed (e.g., 12h/24h switch), clear the entire time row to avoid artifacts
    tft.fillRect(0, max(0, yTop - 2), TFT_W, charH + 4, COL_BG);
    tft.setCursor(x, yTop);
    tft.print(timeStr);
    strncpy(g_lastTime, timeStr, sizeof(g_lastTime)-1);
    g_lastTime[sizeof(g_lastTime)-1] = 0;
    return;
  }

  // Same length: update only changed characters
  for (int i = 0; i < newLen; i++){
    if (g_lastTime[i] != timeStr[i]){
      int cx = x + i * charW;
      tft.fillRect(cx, yTop, charW, charH, COL_BG);
      tft.setCursor(cx, yTop);
      char c[2] = { timeStr[i], 0 };
      tft.print(c);
    }
  }
  strncpy(g_lastTime, timeStr, sizeof(g_lastTime)-1);
  g_lastTime[sizeof(g_lastTime)-1] = 0;
}

// ============================ SET PAGE (Simplified) ============================
static uint8_t eH, eM, eS, eD, eMo, eFmt;
static uint16_t eY;
static bool editInit = true;

const int VIEW_Y = 52;
const int VIEW_H = 214;
int16_t setScroll = 0;
int16_t setScrollMax = 0;

const int BTN_W   = 56;
const int BTN_H   = 32;
const int COL_LX  = 28;
const int BTN_M_X = 220;
const int BTN_V_X = 290;
const int BTN_P_X = 370;

const int ROW_GAP = 40;
const int ROW0_Y  = 62;
enum { IDX_FMT, IDX_DAY, IDX_MON, IDX_YEA, IDX_HOU, IDX_MIN, IDX_SEC, ROW_COUNT };

const int FMT_M_X = 140;
const int FMT_V_X = 200;
const int FMT_V_W = 210;
const int FMT_P_X = 420;

uint8_t daysInMonth(uint16_t Y, uint8_t M){
  if (M==1||M==3||M==5||M==7||M==8||M==10||M==12) return 31;
  if (M==4||M==6||M==9||M==11) return 30;
  bool leap = ((Y%4==0 && Y%100!=0) || (Y%400==0));
  return leap ? 29 : 28;
}

// Draw only the visible part of a rectangle's border within the SET view window
void drawRectBorderClipped(int x, int y, int w, int h, uint16_t color){
  const int y0 = VIEW_Y;
  const int y1 = VIEW_Y + VIEW_H - 1;
  // Top edge
  if (y >= y0 && y <= y1) {
    tft.drawFastHLine(x, y, w, color);
  }
  // Bottom edge
  int by = y + h - 1;
  if (by >= y0 && by <= y1) {
    tft.drawFastHLine(x, by, w, color);
  }
  // Vertical edges (clipped to [y0, y1])
  int sy = max(y, y0);
  int ey = min(y + h - 1, y1);
  if (sy <= ey) {
    tft.drawFastVLine(x, sy, ey - sy + 1, color);
    tft.drawFastVLine(x + w - 1, sy, ey - sy + 1, color);
  }
}

void drawSetRowLabelClipped(int y, const char* label){
  // Only draw when the label is fully inside the viewport to avoid artifacts
  if (y < VIEW_Y || y > VIEW_Y + VIEW_H - 16) return;
  tft.setTextColor(COL_ACC, COL_BG);
  tft.setTextSize(2);
  tft.setCursor(COL_LX, y);
  tft.print(label);
}

void drawButtonClipped(int x,int y,int w,int h,const char* label,uint16_t fg,uint16_t bg){
  if (y+h < VIEW_Y || y > VIEW_Y+VIEW_H) return;
  int yy = max(y, VIEW_Y);
  int hh = min(y+h, VIEW_Y+VIEW_H) - yy;
  tft.fillRect(x, yy, w, hh, bg);
  drawRectBorderClipped(x, y, w, h, COL_BRDR);
  tft.setTextColor(fg,bg); 
  tft.setTextSize(2);
  int16_t tx=x+(w-strlen(label)*12)/2, ty=y+(h-16)/2;
  if (ty >= VIEW_Y && ty <= VIEW_Y+VIEW_H-16) { 
    tft.setCursor(tx,ty); 
    tft.print(label); 
  }
}

void drawValueBoxClippedFit(int x,int y,int w,int h,const char* txt){
  if (y+h < VIEW_Y || y > VIEW_Y+VIEW_H) return;
  int yy = max(y, VIEW_Y);
  int hh = min(y+h, VIEW_Y+VIEW_H) - yy;
  tft.fillRect(x, yy, w, hh, COL_BG);
  drawRectBorderClipped(x, y, w, h, COL_BRDR);

  const uint8_t charW = 12;
  int maxChars = (w - 8) / charW;
  char s[32];
  strncpy(s, txt, 31);
  s[31] = 0;
  
  if ((int)strlen(s) > maxChars && maxChars > 1){
    s[maxChars-1] = '~';
    s[maxChars] = 0;
  }

  tft.setTextColor(COL_FG, COL_BG);
  tft.setTextSize(2);
  int16_t tx = x + (w - strlen(s)*charW)/2;
  int16_t ty = y + (h - 16)/2;
  if (ty >= VIEW_Y && ty <= VIEW_Y+VIEW_H-16) { 
    tft.setCursor(tx,ty); 
    tft.print(s); 
  }
}

int rowY(int idx){ return ROW0_Y + idx*ROW_GAP - setScroll; }

void drawSetPageContent(){
  // Clear the viewport and 1-2px guard bands above/below to prevent residues
  tft.fillRect(0, VIEW_Y, TFT_W, VIEW_H, COL_BG);
  int topY = VIEW_Y - 2; if (topY < 0) topY = 0; int topH = VIEW_Y - topY; if (topH > 0) tft.fillRect(0, topY, TFT_W, topH, COL_BG);
  int botY = VIEW_Y + VIEW_H; int botH = (botY + 2 <= TFT_H) ? 2 : (TFT_H - botY); if (botH > 0) tft.fillRect(0, botY, TFT_W, botH, COL_BG);

  DateTime prevDT(eY, eMo, eD, eH, eM, eS);
  uint8_t prevDOW = prevDT.dayOfTheWeek();

  char buf[32];
  
  // Row: DateFmt
  {
    int y = rowY(IDX_FMT);
    drawSetRowLabelClipped(y, "DateFmt");
    drawButtonClipped(FMT_M_X, y-10, BTN_W, BTN_H, "-", COL_FG, COL_BTN);
    formatDate(buf, prevDOW, eD, eMo, eY, eFmt);
    drawValueBoxClippedFit(FMT_V_X, y-10, FMT_V_W, BTN_H, buf);
    drawButtonClipped(FMT_P_X, y-10, BTN_W, BTN_H, "+", COL_FG, COL_BTN);
  }
  // Row: Day
  {
    int y = rowY(IDX_DAY);
    drawSetRowLabelClipped(y, "Day");
    drawButtonClipped(BTN_M_X, y-10, BTN_W, BTN_H, "-", COL_FG, COL_BTN);
    sprintf(buf, "%02u", eD);
    drawValueBoxClippedFit(BTN_V_X, y-10, BTN_W, BTN_H, buf);
    drawButtonClipped(BTN_P_X, y-10, BTN_W, BTN_H, "+", COL_FG, COL_BTN);
  }
  // Row: Month
  {
    int y = rowY(IDX_MON);
    drawSetRowLabelClipped(y, "Month");
    drawButtonClipped(BTN_M_X, y-10, BTN_W, BTN_H, "-", COL_FG, COL_BTN);
    sprintf(buf, "%02u", eMo);
    drawValueBoxClippedFit(BTN_V_X, y-10, BTN_W, BTN_H, buf);
    drawButtonClipped(BTN_P_X, y-10, BTN_W, BTN_H, "+", COL_FG, COL_BTN);
  }
  // Row: Year
  {
    int y = rowY(IDX_YEA);
    drawSetRowLabelClipped(y, "Year");
    drawButtonClipped(BTN_M_X, y-10, BTN_W, BTN_H, "-", COL_FG, COL_BTN);
    sprintf(buf, "%04u", eY);
    drawValueBoxClippedFit(BTN_V_X, y-10, BTN_W, BTN_H, buf);
    drawButtonClipped(BTN_P_X, y-10, BTN_W, BTN_H, "+", COL_FG, COL_BTN);
  }
  // Row: Hour
  {
    int y = rowY(IDX_HOU);
    drawSetRowLabelClipped(y, "Hour");
    drawButtonClipped(BTN_M_X, y-10, BTN_W, BTN_H, "-", COL_FG, COL_BTN);
    sprintf(buf, "%02u", eH);
    drawValueBoxClippedFit(BTN_V_X, y-10, BTN_W, BTN_H, buf);
    drawButtonClipped(BTN_P_X, y-10, BTN_W, BTN_H, "+", COL_FG, COL_BTN);
  }
  // Row: Minute
  {
    int y = rowY(IDX_MIN);
    drawSetRowLabelClipped(y, "Minute");
    drawButtonClipped(BTN_M_X, y-10, BTN_W, BTN_H, "-", COL_FG, COL_BTN);
    sprintf(buf, "%02u", eM);
    drawValueBoxClippedFit(BTN_V_X, y-10, BTN_W, BTN_H, buf);
    drawButtonClipped(BTN_P_X, y-10, BTN_W, BTN_H, "+", COL_FG, COL_BTN);
  }
  // Row: Second
  {
    int y = rowY(IDX_SEC);
    drawSetRowLabelClipped(y, "Second");
    drawButtonClipped(BTN_M_X, y-10, BTN_W, BTN_H, "-", COL_FG, COL_BTN);
    sprintf(buf, "%02u", eS);
    drawValueBoxClippedFit(BTN_V_X, y-10, BTN_W, BTN_H, buf);
    drawButtonClipped(BTN_P_X, y-10, BTN_W, BTN_H, "+", COL_FG, COL_BTN);
  }

  // Scrollbar
  int contentH = (ROW_COUNT-1)*ROW_GAP + 10;
  setScrollMax = max(0, contentH - (VIEW_H - (ROW0_Y-ROW_GAP)));
  if (setScrollMax > 0){
    int trackX = TFT_W - 8, trackY = VIEW_Y, trackH = VIEW_H;
    tft.fillRect(trackX, trackY, 6, trackH, RGB565(30,30,30));
    int knobH = max(20, (VIEW_H * VIEW_H) / (VIEW_H + setScrollMax));
    int knobY = trackY + (long)setScroll * (trackH - knobH) / setScrollMax;
    tft.fillRect(trackX+1, knobY, 4, knobH, RGB565(90,90,90));
  }
}

void drawSetBottomBar(){
  drawButton(22,  BAR_Y, 140, BAR_H, "RESET SEC", COL_FG, COL_BTN);
  drawButton(180, BAR_Y, 120, BAR_H, "CANCEL",    COL_FG, COL_BTN);
  drawButton(318, BAR_Y, 140, BAR_H, "SAVE",      COL_FG, COL_BTN);
}

void drawSetPage(){
  tft.fillScreen(COL_BG);
  centerText("Set Date & Time", 36, 2, COL_ACC, COL_BG, false);
  centerText("Drag to scroll", 50, 1, COL_DIM, COL_BG, false);

  DateTime n = rtcOK ? rtc.now() : softNow();
  if(editInit){
    eH = n.hour(); eM = n.minute(); eS = n.second();
    eD = n.day();  eMo = n.month();  eY = n.year();
    eFmt = dateFmt;
    editInit = false;
    setScroll = 0;
  }

  drawSetPageContent();
  drawSetBottomBar();

  bool dragging=false; int16_t lastY=0;
  while(uiMode == MODE_SET){
    int16_t sx,sy;
    if(!readTouch(sx,sy)) { dragging=false; delay(10); continue; }

    if (sy >= VIEW_Y && sy < VIEW_Y + VIEW_H){
      if (!dragging){ dragging = true; lastY = sy; }
      int16_t dy = sy - lastY;

      if (abs(dy) >= 6){
        lastY = sy;
        setScroll -= dy;
        if (setScroll < 0) setScroll = 0;
        if (setScroll > setScrollMax) setScroll = setScrollMax;
        drawSetPageContent(); drawSetBottomBar(); continue;
      }

      auto hitRow = [&](int idx, int bx, int /*by*/, int bw, int bh)->bool{
        int y = rowY(idx) - 10;
        return inRect(sx, sy, bx, y, bw, bh);
      };

      bool updated=false;

      if(hitRow(IDX_FMT, FMT_M_X, 0, BTN_W, BTN_H)) { eFmt = (eFmt==0 ? 4 : eFmt-1); updated=true; }
      else if(hitRow(IDX_FMT, FMT_P_X, 0, BTN_W, BTN_H)) { eFmt = (eFmt==4 ? 0 : eFmt+1); updated=true; }
      else if(hitRow(IDX_DAY, BTN_M_X, 0, BTN_W, BTN_H)) { eD = (eD==1 ? daysInMonth(eY,eMo) : eD-1); updated=true; }
      else if(hitRow(IDX_DAY, BTN_P_X, 0, BTN_W, BTN_H)) { uint8_t dim=daysInMonth(eY,eMo); eD=(eD==dim?1:eD+1); updated=true; }
      else if(hitRow(IDX_MON, BTN_M_X, 0, BTN_W, BTN_H)) { eMo=(eMo==1?12:eMo-1); if(eD>daysInMonth(eY,eMo)) eD=daysInMonth(eY,eMo); updated=true; }
      else if(hitRow(IDX_MON, BTN_P_X, 0, BTN_W, BTN_H)) { eMo=(eMo==12?1:eMo+1); if(eD>daysInMonth(eY,eMo)) eD=daysInMonth(eY,eMo); updated=true; }
      else if(hitRow(IDX_YEA, BTN_M_X, 0, BTN_W, BTN_H)) { eY=(eY>2000?eY-1:2099); if(eD>daysInMonth(eY,eMo)) eD=daysInMonth(eY,eMo); updated=true; }
      else if(hitRow(IDX_YEA, BTN_P_X, 0, BTN_W, BTN_H)) { eY=(eY<2099?eY+1:2000); if(eD>daysInMonth(eY,eMo)) eD=daysInMonth(eY,eMo); updated=true; }
      else if(hitRow(IDX_HOU, BTN_M_X, 0, BTN_W, BTN_H)) { eH=(eH+23)%24; updated=true; }
      else if(hitRow(IDX_HOU, BTN_P_X, 0, BTN_W, BTN_H)) { eH=(eH+1)%24;  updated=true; }
      else if(hitRow(IDX_MIN, BTN_M_X, 0, BTN_W, BTN_H)) { eM=(eM+59)%60; updated=true; }
      else if(hitRow(IDX_MIN, BTN_P_X, 0, BTN_W, BTN_H)) { eM=(eM+1)%60;  updated=true; }
      else if(hitRow(IDX_SEC, BTN_M_X, 0, BTN_W, BTN_H)) { eS=(eS+59)%60; updated=true; }
      else if(hitRow(IDX_SEC, BTN_P_X, 0, BTN_W, BTN_H)) { eS=(eS+1)%60;  updated=true; }

      if(updated){ drawSetPageContent(); drawSetBottomBar(); delay(100); }
    }
    else if (sy >= BAR_Y && sy < BAR_Y + BAR_H){
      if (inRect(sx, sy, 22, BAR_Y, 140, BAR_H)) {
        eS = 0; drawSetPageContent(); drawSetBottomBar(); delay(110);
      }
      else if (inRect(sx, sy, 180, BAR_Y, 120, BAR_H)) {
        uiMode = MODE_CLOCK;
      }
      else if (inRect(sx, sy, 318, BAR_Y, 140, BAR_H)) {
        DateTime newdt(eY, eMo, eD, eH, eM, eS);
        if(rtcOK) rtc.adjust(newdt); else softAdjust(newdt);
        dateFmt = eFmt; EEPROM.update(EEPROM_ADDR_DATEFMT, dateFmt);
        uiMode = MODE_CLOCK;
      }
    }
  }
  editInit = true;
}

// ============================ STOPWATCH ============================
void formatMs(char* buf, uint32_t ms){
  uint16_t cs = (uint16_t)((ms % 1000) / 10);
  uint32_t sec = ms / 1000;
  uint16_t mm = (uint16_t)((sec / 60) % 100);
  uint16_t ss = (uint16_t)(sec % 60);
  sprintf(buf, "%02u:%02u.%02u", (unsigned)mm, (unsigned)ss, (unsigned)cs);
}

void updateStopwatchUI(bool force){
  uint32_t now=millis(); 
  if(!force&&(now-swLastDraw)<50)return; 
  swLastDraw=now;
  uint32_t elapsed=swAccumMs+(swRunning?(now-swStartMs):0);
  
  char buf[16];
  formatMs(buf, elapsed);
  drawStopwatchTimeSmooth(buf);
  
  // Update lap lines only on force refresh to avoid flicker while running
  if (force){
    for(int i=0;i<4;i++){
      char line[32];
      if(i<swLapCount){
        formatMs(buf, swLap[i]);
        sprintf(line, "Lap %d: %s", i+1, buf);
      } else {
        line[0] = 0;
      }
      int y=210+i*18; 
      tft.fillRect(60,y-14,360,20,COL_BG);
      tft.setTextColor(COL_DIM,COL_BG); 
      tft.setTextSize(2); 
      tft.setCursor(60,y-14); 
      tft.print(line);
    }
  }
}

void drawStopwatchFrame(){
  tft.fillScreen(COL_BG);
  centerText("Stopwatch",42,2,COL_ACC,COL_BG,false);
  centerText("START / LAP / RESET / BACK",80,2,COL_DIM,COL_BG,false);
  invalidateStopwatchText();
  updateStopwatchUI(true);
  drawButton(SW_START_X,SW_Y,SW_BTN_W,SW_BTN_H,swRunning?"STOP":"START",COL_FG,COL_BTN);
  drawButton(SW_LAP_X,  SW_Y,SW_BTN_W,SW_BTN_H,"LAP",   COL_FG,COL_BTN);
  drawButton(SW_RESET_X,SW_Y,SW_BTN_W,SW_BTN_H,"RESET", COL_FG,COL_BTN);
  drawButton(SW_BACK_X, SW_Y,SW_BTN_W,SW_BTN_H,"BACK",  COL_FG,COL_BTN);
}
// Invalidate stopwatch cached text
void invalidateStopwatchText(){ g_lastSw[0] = 0; }

// Smooth stopwatch time redraw similar to clock's smooth time
void drawStopwatchTimeSmooth(const char* txt){
  const uint8_t size = 6;
  const uint8_t charW = 6 * size;
  const uint8_t charH = 8 * size;
  const int yCenter = 160;
  const int yTop = yCenter - charH;

  tft.setTextColor(COL_FG, COL_BG);
  tft.setTextSize(size);

  int newLen = (int)strlen(txt);
  int oldLen = (int)strlen(g_lastSw);
  int w = newLen * charW;
  int x = (TFT_W - w) / 2;

  if (newLen != oldLen) {
    tft.fillRect(0, max(0, yTop - 2), TFT_W, charH + 4, COL_BG);
    tft.setCursor(x, yTop);
    tft.print(txt);
    strncpy(g_lastSw, txt, sizeof(g_lastSw)-1);
    g_lastSw[sizeof(g_lastSw)-1] = 0;
    return;
  }

  for (int i=0;i<newLen;i++){
    if (g_lastSw[i] != txt[i]){
      int cx = x + i*charW;
      tft.fillRect(cx, yTop, charW, charH, COL_BG);
      tft.setCursor(cx, yTop);
      char c[2] = { txt[i], 0 };
      tft.print(c);
    }
  }
  strncpy(g_lastSw, txt, sizeof(g_lastSw)-1);
  g_lastSw[sizeof(g_lastSw)-1] = 0;
}

// ============================ SETUP / LOOP ============================
void setup(){
  if(rtc.begin()){
    rtcOK=true;
    if(rtc.lostPower()) rtc.adjust(DateTime(F(__DATE__),F(__TIME__)));
  } else {
    rtcOK=false;
    softAdjust(DateTime(soft.year,soft.month,soft.day,soft.hour,soft.minute,soft.second));
  }

  is24h = (EEPROM.read(EEPROM_ADDR_FMT_12_24)==1);
  uint8_t df = EEPROM.read(EEPROM_ADDR_DATEFMT); 
  dateFmt = (df<=4 ? df : 0);

  uint16_t id=tft.readID(); 
  if(id==0xD3D3)id=0x9488;
  tft.begin(id);
  tft.setRotation(1);
  tft.setTextWrap(false);

  drawClockFrame();
}

void loop(){
  static bool touchDown=false;
  int16_t sx,sy;
  bool pressed = readTouch(sx,sy);
  if(pressed && !touchDown){
    touchDown=true;
    if(uiMode==MODE_CLOCK){
      if(inRect(sx,sy,BTN_FMT_X,BTN_FMT_Y,CLK_BTN_W,BAR_H)){
        is24h=!is24h; 
        EEPROM.update(EEPROM_ADDR_FMT_12_24,is24h?1:0);
        drawButton(BTN_FMT_X,BTN_FMT_Y,CLK_BTN_W,BAR_H,is24h?"FMT:24H":"FMT:12H",COL_FG,COL_BTN);
        // Immediately refresh time/date to reflect new format
        invalidateClockText();
        drawClockNow();
      } else if(inRect(sx,sy,BTN_SW_X,BTN_SW_Y,CLK_BTN_W,BAR_H)){
        // Preserve stopwatch state when entering the screen
        uiMode=MODE_STOPWATCH; 
        drawStopwatchFrame();
      } else if(inRect(sx,sy,BTN_SET_X,BTN_SET_Y,CLK_BTN_W,BAR_H)){
        uiMode=MODE_SET; 
        drawSetPage(); 
        drawClockFrame();
      }
    }
    else if(uiMode==MODE_STOPWATCH){
      if(inRect(sx,sy,SW_START_X,SW_Y,SW_BTN_W,SW_BTN_H)){
        if(!swRunning){ 
          swRunning=true; 
          swStartMs=millis(); 
          drawButton(SW_START_X,SW_Y,SW_BTN_W,SW_BTN_H,"STOP",COL_FG,COL_BTN); 
          updateStopwatchUI(true);
        }
        else { 
          swAccumMs+=millis()-swStartMs; 
          swRunning=false; 
          drawButton(SW_START_X,SW_Y,SW_BTN_W,SW_BTN_H,"START",COL_FG,COL_BTN); 
          updateStopwatchUI(true);
        }
      } else if(inRect(sx,sy,SW_LAP_X,SW_Y,SW_BTN_W,SW_BTN_H)){
        uint32_t now=millis();
        uint32_t elapsed=swAccumMs+(swRunning?(now-swStartMs):0);
        if(swLapCount<4) swLap[swLapCount++]=elapsed; 
        updateStopwatchUI(true);
      } else if(inRect(sx,sy,SW_RESET_X,SW_Y,SW_BTN_W,SW_BTN_H)){
        swRunning=false; 
        swAccumMs=0; 
        swLapCount=0; 
        drawButton(SW_START_X,SW_Y,SW_BTN_W,SW_BTN_H,"START",COL_FG,COL_BTN); 
        updateStopwatchUI(true);
      } else if(inRect(sx,sy,SW_BACK_X,SW_Y,SW_BTN_W,SW_BTN_H)){
        uiMode=MODE_CLOCK; 
        drawClockFrame();
      }
    }
  } else if(!pressed && touchDown){
    touchDown=false;
  }

  if(uiMode==MODE_CLOCK){
    uint32_t nowMs=millis();
    if(nowMs-lastTickMs>=200){
      lastTickMs=nowMs;
      static uint8_t prevS=255;
      uint8_t h,m,s,dd,mm,dow; 
      uint16_t yyyy;
      getNow(h,m,s,dd,mm,yyyy,dow);
      if(s!=prevS){
        prevS=s;
        char timeStr[20], dateStr[32];
        formatTime(timeStr, h, m, s, is24h);
        formatDate(dateStr, dow, dd, mm, yyyy, dateFmt);
        drawClockLines(timeStr, dateStr);
      }
    }
  } else if(uiMode==MODE_STOPWATCH){
    updateStopwatchUI(false);
  }
}
