// ============================ STOPWATCH (Module) ============================

// Cached stopwatch last text for smooth updates
char g_lastSw[16] = "";

void invalidateStopwatchText(){ g_lastSw[0] = 0; }

void formatMs(char* buf, uint32_t ms){
  uint16_t cs = (uint16_t)((ms % 1000) / 10);
  uint32_t sec = ms / 1000;
  uint16_t mm = (uint16_t)((sec / 60) % 100);
  uint16_t ss = (uint16_t)(sec % 60);
  sprintf(buf, "%02u:%02u.%02u", (unsigned)mm, (unsigned)ss, (unsigned)cs);
}

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

