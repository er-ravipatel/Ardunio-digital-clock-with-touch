// ============================ SET PAGE (Module) ============================

static uint8_t eH, eM, eS, eD, eMo, eFmt;
static uint16_t eY;
// Snapshot of values on entry for RESET behavior
static uint8_t eH0, eM0, eS0, eD0, eMo0, eFmt0;
static uint16_t eY0;
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

// Redraw only the value box of a given row (minimal refresh)
void drawSetRowValue(int idx){
  char buf[32];
  int y = rowY(idx);
  DateTime prevDT(eY, eMo, eD, eH, eM, eS);
  uint8_t prevDOW = prevDT.dayOfTheWeek();

  switch(idx){
    case IDX_FMT:
      formatDate(buf, prevDOW, eD, eMo, eY, eFmt);
      drawValueBoxClippedFit(FMT_V_X, y-10, FMT_V_W, BTN_H, buf);
      break;
    case IDX_DAY:
      sprintf(buf, "%02u", eD);
      drawValueBoxClippedFit(BTN_V_X, y-10, BTN_W, BTN_H, buf);
      break;
    case IDX_MON:
      sprintf(buf, "%02u", eMo);
      drawValueBoxClippedFit(BTN_V_X, y-10, BTN_W, BTN_H, buf);
      break;
    case IDX_YEA:
      sprintf(buf, "%04u", eY);
      drawValueBoxClippedFit(BTN_V_X, y-10, BTN_W, BTN_H, buf);
      break;
    case IDX_HOU:
      sprintf(buf, "%02u", eH);
      drawValueBoxClippedFit(BTN_V_X, y-10, BTN_W, BTN_H, buf);
      break;
    case IDX_MIN:
      sprintf(buf, "%02u", eM);
      drawValueBoxClippedFit(BTN_V_X, y-10, BTN_W, BTN_H, buf);
      break;
    case IDX_SEC:
      sprintf(buf, "%02u", eS);
      drawValueBoxClippedFit(BTN_V_X, y-10, BTN_W, BTN_H, buf);
      break;
  }
}

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
  drawButton(22,  BAR_Y, 140, BAR_H, "RESET", COL_FG, COL_BTN);
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
    // take snapshot for RESET
    eH0=eH; eM0=eM; eS0=eS; eD0=eD; eMo0=eMo; eY0=eY; eFmt0=eFmt;
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

      bool updated=false; int changedIdx=-1;

      if(hitRow(IDX_FMT, FMT_M_X, 0, BTN_W, BTN_H)) { eFmt = (eFmt==0 ? 4 : eFmt-1); updated=true; changedIdx=IDX_FMT; }
      else if(hitRow(IDX_FMT, FMT_P_X, 0, BTN_W, BTN_H)) { eFmt = (eFmt==4 ? 0 : eFmt+1); updated=true; changedIdx=IDX_FMT; }
      else if(hitRow(IDX_DAY, BTN_M_X, 0, BTN_W, BTN_H)) { eD = (eD==1 ? daysInMonth(eY,eMo) : eD-1); updated=true; changedIdx=IDX_DAY; }
      else if(hitRow(IDX_DAY, BTN_P_X, 0, BTN_W, BTN_H)) { uint8_t dim=daysInMonth(eY,eMo); eD=(eD==dim?1:eD+1); updated=true; changedIdx=IDX_DAY; }
      else if(hitRow(IDX_MON, BTN_M_X, 0, BTN_W, BTN_H)) { eMo=(eMo==1?12:eMo-1); if(eD>daysInMonth(eY,eMo)) eD=daysInMonth(eY,eMo); updated=true; changedIdx=IDX_MON; }
      else if(hitRow(IDX_MON, BTN_P_X, 0, BTN_W, BTN_H)) { eMo=(eMo==12?1:eMo+1); if(eD>daysInMonth(eY,eMo)) eD=daysInMonth(eY,eMo); updated=true; changedIdx=IDX_MON; }
      else if(hitRow(IDX_YEA, BTN_M_X, 0, BTN_W, BTN_H)) { eY=(eY>2000?eY-1:2099); if(eD>daysInMonth(eY,eMo)) eD=daysInMonth(eY,eMo); updated=true; changedIdx=IDX_YEA; }
      else if(hitRow(IDX_YEA, BTN_P_X, 0, BTN_W, BTN_H)) { eY=(eY<2099?eY+1:2000); if(eD>daysInMonth(eY,eMo)) eD=daysInMonth(eY,eMo); updated=true; changedIdx=IDX_YEA; }
      else if(hitRow(IDX_HOU, BTN_M_X, 0, BTN_W, BTN_H)) { eH=(eH+23)%24; updated=true; changedIdx=IDX_HOU; }
      else if(hitRow(IDX_HOU, BTN_P_X, 0, BTN_W, BTN_H)) { eH=(eH+1)%24;  updated=true; changedIdx=IDX_HOU; }
      else if(hitRow(IDX_MIN, BTN_M_X, 0, BTN_W, BTN_H)) { eM=(eM+59)%60; updated=true; changedIdx=IDX_MIN; }
      else if(hitRow(IDX_MIN, BTN_P_X, 0, BTN_W, BTN_H)) { eM=(eM+1)%60;  updated=true; changedIdx=IDX_MIN; }
      else if(hitRow(IDX_SEC, BTN_M_X, 0, BTN_W, BTN_H)) { eS=(eS+59)%60; updated=true; changedIdx=IDX_SEC; }
      else if(hitRow(IDX_SEC, BTN_P_X, 0, BTN_W, BTN_H)) { eS=(eS+1)%60;  updated=true; changedIdx=IDX_SEC; }

      if(updated){ 
        drawSetRowValue(changedIdx);
        if (changedIdx==IDX_FMT || changedIdx==IDX_DAY || changedIdx==IDX_MON || changedIdx==IDX_YEA){
          drawSetRowValue(IDX_FMT);
        }
        delay(80); 
      }
    }
    else if (sy >= BAR_Y && sy < BAR_Y + BAR_H){
      if (inRect(sx, sy, 22, BAR_Y, 140, BAR_H)) {
        // RESET: restore snapshot and redraw visible content
        eH=eH0; eM=eM0; eS=eS0; eD=eD0; eMo=eMo0; eY=eY0; eFmt=eFmt0;
        drawSetPageContent();
        drawSetBottomBar();
        delay(110);
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
