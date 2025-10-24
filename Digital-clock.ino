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
void drawTimeNow();
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

// Draw only the current time (keep date untouched)
void drawTimeNow(){
  uint8_t h,m,s,dd,mm,dow; 
  uint16_t yyyy;
  getNow(h,m,s,dd,mm,yyyy,dow);
  char timeStr[20];
  formatTime(timeStr, h, m, s, is24h);
  drawTimeSmooth(timeStr);
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
        // Immediately refresh time only (keep date untouched)
        drawTimeNow();
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
