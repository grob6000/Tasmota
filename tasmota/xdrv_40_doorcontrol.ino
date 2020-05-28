/*
  xdrv_40_doorcontrol.ino - Basic door control support for Tasmota

  Copyright (C) 2020  George Robinson

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#define DEBUG_COVER
#ifdef USE_COVER
/*********************************************************************************************\
 * Basic door control - relay control, switch sensors
\*********************************************************************************************/

#define XDRV_40            40

#define COVER_FOLLOWUPINTERVAL 60 // seconds
#define COVER_DOUBLETAPINTERVAL 3 // seconds

#define D_PRFX_COVER "Cover"

#define D_CMND_COVER_OPEN "Open"
#define D_CMND_COVER_CLOSE "Close"
#define D_CMND_COVER_TOGGLE "Toggle"
#define D_CMND_COVER_STOP "Stop"
#define D_CMND_COVER_CONFIG "Config"

#define D_STAT_COVER_OPEN "Open"
#define D_STAT_COVER_CLOSED "Closed"
#define D_STAT_COVER_OPENING "Opening"
#define D_STAT_COVER_CLOSING "Closing"
#define D_STAT_COVER_OBSTRUCTED "Obstructed"
#define D_STAT_COVER_UNKNOWN "Unknown"
#define D_STAT_COVER_STOPPED "Stopped"

typedef enum { CS_CLOSED, CS_OPEN, CS_CLOSING, CS_OPENING, CS_STOPPED, CS_OBSTRUCTED, CS_UNKNOWN } coverstate_t;

coverstate_t coverStates[MAX_COVERS];
coverstate_t coverTarget[MAX_COVERS];
uint8_t coverFollowUp[MAX_COVERS];

// template for outputting state
const char JSON_COVER_STATE[] PROGMEM = "\"" D_PRFX_COVER "%d\":{\"State\":%s,\"Position\":%d}";
const char S_JSON_COMMAND_COVER_CONFIG[] PROGMEM = "{\"" D_PRFX_COVER D_CMND_COVER_CONFIG "%d\":[%d,%d,%d,%d,%d,%d]}";



#define COVER_STATE_TEXT_MAXLEN 10

const char kCoverCommands[] PROGMEM = D_PRFX_COVER "|"
  D_CMND_COVER_OPEN "|" D_CMND_COVER_CLOSE "|" D_CMND_COVER_TOGGLE "|" D_CMND_COVER_STOP "|"
  D_CMND_COVER_CONFIG;

const uint8_t kCoverPositions[] PROGMEM = {0, 100, 25, 75, 50, 50, 50};
  
void (* const CoverCommand[])(void) PROGMEM = {
  &CmndCoverOpen, &CmndCoverClose, &CmndCoverToggle, &CmndCoverStop,
  &CmndCoverConfig
};
  
// in settings.h:
//   CoverCfg     cover_cfg[MAX_COVERS]; // F42 - settings for door control, 3B each

const char kCoverStateText[] PROGMEM = D_STAT_COVER_CLOSED "|" D_STAT_COVER_OPEN "|" D_STAT_COVER_CLOSING "|" D_STAT_COVER_OPENING "|"
                                      D_STAT_COVER_STOPPED "|" D_STAT_COVER_OBSTRUCTED "|" D_STAT_COVER_UNKNOWN ;
                                      
// return string representing state
char *coverGetStateText(uint32_t coverindex) {
  char statetext[COVER_STATE_TEXT_MAXLEN];
  return GetTextIndexed(statetext, sizeof(statetext), coverStates[coverindex], kCoverStateText);
}

// return dummy position (0-100, HAss defalt style) based on state
uint8_t coverGetPosition(uint32_t coverindex) {
  return kCoverPositions[coverStates[coverindex]];
}

bool Xdrv40(uint8_t function)
{
  bool result = false;

  switch (function) {
    case FUNC_PRE_INIT:
      CoverInit();
      result = true;
      break;
    //case FUNC_EVERY_50_MSECOND:
    //case FUNC_EVERY_250_MSECOND:
    case FUNC_EVERY_SECOND:
      coverCheckState();
      result = true;
      break;
    case FUNC_COMMAND:
      result = DecodeCommand(kCoverCommands, CoverCommand);
      break;
    case FUNC_JSON_APPEND:
      for (uint32_t i = 0; i < MAX_COVERS; i++) {
        if (Settings.cover_cfg[i].enabled) {
          ResponseAppend_P(",");
          ResponseAppend_P(JSON_COVER_STATE, i+1, coverGetStateText(i), coverGetPosition(i));
        }
      }
      result = true;
      break;
  }
  return result;    
}

// init on boot
void CoverInit(void)
{
  // on boot, state and target state is unknown
#ifdef DEBUG_COVER
  AddLog_P2(LOG_LEVEL_DEBUG, PSTR("CoverInit MAX_COVERS=%d"), MAX_COVERS);
#endif
  for (uint32_t i = 0; i < MAX_COVERS; i++) {
    coverTarget[i] = CS_UNKNOWN;
    coverStates[i] = CS_UNKNOWN;
  }
}

// check state based on sensor input
void coverCheckState()
{
  for (uint32_t i = 0; i < MAX_COVERS; i++) {
    coverstate_t newstate = CS_UNKNOWN;
    if (Settings.cover_cfg[i].switch_obstruct && SwitchState(Settings.cover_cfg[i].switch_obstruct-1)) {
      // obstruction sensor exists and ON
      newstate = CS_OBSTRUCTED;
    } else if (Settings.cover_cfg[i].switch_open) {
      if (SwitchState(Settings.cover_cfg[i].switch_open-1)) {
        // open sensor exists and ON
        newstate = CS_OPEN;
      } else if (!(Settings.cover_cfg[i].switch_closed)) {
        // open sensor exists, is OFF, and there is no closed switch - assume closed
        newstate = CS_CLOSED;
      }
    } else if (Settings.cover_cfg[i].switch_closed) {
      if (SwitchState(Settings.cover_cfg[i].switch_closed-1)) {
        // closed sensor exists and ON
        newstate = CS_CLOSED;
      } else if (!(Settings.cover_cfg[i].switch_open)) {
        // closed sensor exists, is OFF, and there is no open switch - assume open
        newstate = CS_OPEN;        
      }
    }
//#ifdef DEBUG_COVER
//    AddLog_P2(LOG_LEVEL_DEBUG, PSTR("%dA: newstate=%d, coverstate=%d"), i, newstate, coverStates[i]);
//#endif    
    if (newstate == CS_UNKNOWN) { // didn't get a state from sensor input
      switch (coverStates[i]) { // check last known state
        case CS_CLOSED: // change from closed to unknown - probably opening
        case CS_OPENING: // was opening, probably still is
          newstate = CS_OPENING;
          break;
        case CS_OPEN: // change from open to unknown - probably closing
        case CS_CLOSING: // was closing, probably still is
          newstate = CS_CLOSING;
          break;
        case CS_STOPPED:
          newstate = CS_STOPPED; // was stopped, probably still stopped
          break;
      }
    }        
    coverStates[i] = newstate; 
//#ifdef DEBUG_COVER
//    AddLog_P2(LOG_LEVEL_DEBUG, PSTR("%dB: coverstate=%d"), i, coverStates[i]);
//#endif
    // followup
    if (coverFollowUp[i] > 0) {
      coverFollowUp[i]--;
#ifdef DEBUG_COVER
      AddLog_P2(LOG_LEVEL_DEBUG, PSTR("Followup countdown i=%d, t-%d"), i, coverFollowUp[i]);
#endif
    }
    if (coverStates[i] == coverTarget[i]) {
      coverFollowUp[i] = 0;
    } else if (coverFollowUp[i] == 1) {
      // door hasn't gone where it should; hit the button again (followup only in toggle mode)
      ExecuteCommandPower(Settings.cover_cfg[i].relay_open, POWER_ON, SRC_IGNORE);
      coverFollowUp[i] = COVER_FOLLOWUPINTERVAL; // round again and again!
#ifdef DEBUG_COVER
      AddLog_P2(LOG_LEVEL_DEBUG, PSTR("Followup timeout i=%d"), i);
#endif
    }
  }
#ifdef DEBUG_COVER
  AddLog_P2(LOG_LEVEL_DEBUG, PSTR("cs1=%d, cs2=%d"), coverStates[0], coverStates[1]);
#endif
}

// test: if relay_open is assigned, but relay_close is not, then we consider relay_open to be a toggle relay
bool CoverIsToggleMode(uint8_t coverindex)
{
  return ((Settings.cover_cfg[coverindex].relay_open)&&(!Settings.cover_cfg[coverindex].relay_close));
}


/*********************************************************************************************\
 * Commands
\*********************************************************************************************/

void CmndCoverOpen(void) {
  if (!(XdrvMailbox.index)) { XdrvMailbox.index = 1; };
  if ((XdrvMailbox.index <= MAX_COVERS)) {
    uint32_t coverindex = XdrvMailbox.index - 1;
    if (Settings.cover_cfg[coverindex].relay_open && !(CoverIsToggleMode(coverindex) && ((coverStates[coverindex] == CS_OPEN) || (coverStates[coverindex] == CS_OPENING)))) {
      ExecuteCommandPower(Settings.cover_cfg[coverindex].relay_open, POWER_ON, SRC_IGNORE);
      if (CoverIsToggleMode(coverindex)) {
        coverFollowUp[coverindex] = COVER_FOLLOWUPINTERVAL; // follow this one up...
      }
      ResponseCmndDone();
    } 
  }
}

void CmndCoverClose(void) {
  if (!(XdrvMailbox.index)) { XdrvMailbox.index = 1; };
  if ((XdrvMailbox.index <= MAX_COVERS)) {
    uint32_t coverindex = XdrvMailbox.index - 1;
    uint8_t relay = Settings.cover_cfg[coverindex].relay_close;
    if (CoverIsToggleMode(coverindex)) {
      if ((coverStates[coverindex] == CS_CLOSED) || (coverStates[coverindex] == CS_CLOSING)) {
        relay = 0;
      } else {
        relay = Settings.cover_cfg[coverindex].relay_open;
        coverFollowUp[coverindex] = COVER_FOLLOWUPINTERVAL; // follow this one up...
      }
    }
    if (relay) {
      ExecuteCommandPower(relay, POWER_ON, SRC_IGNORE);
      coverTarget[coverindex] = CS_CLOSED;
      ResponseCmndDone();
    }
  } 
}

void CmndCoverStop(void) {
  if (!(XdrvMailbox.index)) { XdrvMailbox.index = 1; };
  if ((XdrvMailbox.index <= MAX_COVERS)) { 
    uint32_t coverindex = XdrvMailbox.index - 1;
    uint8_t relay = 0;
    if (Settings.cover_cfg[coverindex].relay_stop) {
      relay = Settings.cover_cfg[coverindex].relay_stop;
    } else if (CoverIsToggleMode(coverindex)) {
      relay = Settings.cover_cfg[coverindex].relay_open;
    }
    if (relay) {
      ExecuteCommandPower(relay, POWER_ON, SRC_IGNORE);
      coverStates[coverindex] = CS_STOPPED; // optimistic for non-sensed state
      ResponseCmndDone();
    }
  }
}

void CmndCoverToggle(void) {
  if (!(XdrvMailbox.index)) { XdrvMailbox.index = 1; };
  if ((XdrvMailbox.index <= MAX_COVERS)) {
    uint32_t coverindex = XdrvMailbox.index - 1;
    switch (coverStates[coverindex]) {
      case CS_OPEN:
        CmndCoverClose();
        break;
      case CS_CLOSED:
        CmndCoverOpen();
        break;
      case CS_OPENING:
      case CS_CLOSING:
        CmndCoverStop();
        break;
      case CS_STOPPED:
      case CS_UNKNOWN:
      case CS_OBSTRUCTED:
        // toggle based on target; otherwise close
        if (coverTarget[coverindex] == CS_CLOSED) {
          CmndCoverOpen();
        } else {
          CmndCoverClose();
        }
        break;
    }
    // response handled by close/open functions        
  }
}


// CoverConfig<index> <relay_open>, <relay_close>, <relay_stop>, <switch_open>, <switch_closed>, <switch_obstruct>
// e.g. CoverRelays1 1, 0, 0, 1, 2, 0
                    
void CmndCoverConfig(void) {
  if (!(XdrvMailbox.index)) { XdrvMailbox.index = 1; };
  if ((XdrvMailbox.index <= MAX_COVERS)) {
    uint32_t coverindex = XdrvMailbox.index - 1;
    uint32_t p[6] = {0};
    uint32_t pcount = ParseParameters(6, p);
#ifdef DEBUG_COVER
    AddLog_P2(LOG_LEVEL_DEBUG, PSTR("p=[%d,%d,%d,%d,%d,%d]"), p[0], p[1], p[2], p[3], p[4], p[5]);
#endif
    CoverParam *cp = &Settings.cover_cfg[coverindex];
    if (pcount > 0) {
      cp->relay_open = p[0];
    }
    if (pcount > 1) {
      cp->relay_close = p[1];
    }
    if (pcount > 2) {
      cp->relay_stop = p[2];
    }
    if (pcount > 3) {
      cp->switch_open = p[3];
    }
    if (pcount > 4) {
      cp->switch_closed = p[4];
    }
    if (pcount > 5) {
      cp->switch_obstruct = p[5];
    }
    cp->enabled = (cp->relay_open || cp->relay_close || cp->relay_stop || cp->switch_open || cp->switch_closed || cp->switch_obstruct) ? 1 : 0;
    // response example: {"CoverConfig1":[1,0,0,1,2,0]}
    Response_P(S_JSON_COMMAND_COVER_CONFIG, XdrvMailbox.index, cp->relay_open, cp->relay_close, cp->relay_stop, cp->switch_open, cp->switch_closed, cp->switch_obstruct);
#ifdef DEBUG_COVER
    AddLog_P2(LOG_LEVEL_DEBUG, PSTR("i=%d, pcount=%d, config=[%d,%d,%d,%d,%d,%d]"), coverindex, pcount, Settings.cover_cfg[coverindex].relay_open, Settings.cover_cfg[coverindex].relay_close, Settings.cover_cfg[coverindex].relay_stop, Settings.cover_cfg[coverindex].switch_open, Settings.cover_cfg[coverindex].switch_closed, Settings.cover_cfg[coverindex].switch_obstruct);
#endif
  }
}

#endif //USE_COVER