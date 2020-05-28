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

#ifdef USE_DOORCONTROL
/*********************************************************************************************\
 * Basic door control - relay control, switch sensors
\*********************************************************************************************/

#define XDRV_40            40
#define COVER_MAXCOUNT      2 // maximum number of doors supported - do not change or it will derp settings

#define COVER_FLAG_FILTERCOMMANDS 0x01
#define COVER_FLAG_STUBBORN 0x02

#define COVER_FOLLOWUPINTERVAL 60 // seconds
#define COVER_DOUBLETAPINTERVAL 3 // seconds

typedef enum coverstate_t { CS_CLOSED, CS_OPEN, CS_CLOSING, CS_OPENING, CS_STOPPED, CS_OBSTRUCTED, CS_UNKNOWN };

coverstate_t coverStates[COVER_MAXCOUNT];
coverstate_t coverTarget[COVER_MAXCOUNT];
uint8_t coverFollowUp[COVER_MAXCOUNT] = 0;

// config - 4b allows choice from 16 relays/switches (8 currently possible, but 4 works with storage boundaries better)
typedef struct CoverCfg {
  uint8_t relay_open : 4;
  uint8_t relay_close: 4;
  uint8_t relay_stop: 4;
  uint8_t switch_open: 4;
  uint8_t switch_closed: 4;
  uint8_t switch_obstruct: 4;
};

// template for outputting state
const char JSON_COVER_STATE[] PROGMEM = "\"" D_PRFX_COVER "%d\":{\"State\":%s,\"Position\":%d}";
const char S_JSON_COMMAND_COVER_RELAYS[] PROGMEM = "{\"" D_PRFX_COVER D_CMND_RELAYS "%d\":{\"Open\":%d,\"Close\":%d,\"Stop\":%d}}";
const char S_JSON_COMMAND_COVER_SWITCHES[] PROGMEM = "{\"" D_PRFX_COVER D_CMND_SWITCHES "%d\":{\"Open\":%d,\"Closed\":%d,\"Obstructed\":%d}}";

#define D_PRFX_COVER "Cover"
#define D_CMND_COVER_OPEN "Open"
#define D_CMND_COVER_CLOSE "Close"
#define D_CMND_COVER_TOGGLE "Toggle"
#define D_CMND_COVER_STOP "Stop"
#define D_CMND_COVER_RELAYS "Relays"
#define D_CMND_COVER_SWITCHES "Switches"
#define D_CMND_COVER_STATE "State"
#define D_STAT_COVER_OPEN "Open"
#define D_STAT_COVER_CLOSED "Closed"
#define D_STAT_COVER_OPENING "Opening"
#define D_STAT_COVER_CLOSING "Closing"
#define D_STAT_COVER_OBSTRUCTED "Obstructed"
#define D_STAT_COVER_UNKNOWN "Unknown"
#define D_STAT_COVER_STOPPED "Stopped"

#define COVER_STATE_TEXT_MAXLEN 10

const char kCoverCommands[] PROGMEM = D_PRFX_COVER "|"
  D_CMND_COVER_OPEN "|" D_CMND_COVER_CLOSE "|" D_CMND_COVER_TOGGLE "|" D_CMND_COVER_STOP "|"
  D_CMND_COVER_RELAYS "|" D_CMND_COVER_SWITCHES "|" D_CMND_COVER_STATE;

const uint8_t kCoverPositions[] PROGMEM = {0, 100, 25, 75, 50, 50, 50};
  
void (* const CoverCommand[])(void) PROGMEM = {
  &CmndCoverOpen, &CmndCoverClose, &CmndCoverToggle, &CmndCoverStop,
  &CmndCoverRelays, &CmndCoverSwitches, &CmndCoverState
};
  
// in settings.h:
//   CoverCfg     cover_cfg[COVER_MAXCOUNT]; // F42 - settings for door control, 3B each

const char kCoverStateText[] PROGMEM = D_STAT_COVER_CLOSED "|" D_STAT_COVER_OPEN "|" D_STAT_COVER_CLOSING "|" D_STAT_COVER_OPENING "|"
                                      D_STAT_COVER_STOPPED "|" D_STAT_COVER_OBSTRUCTED "|" D_STAT_COVER_UNKNOWN
                                      
// return string representing state
char[] coverGetStateText(uint8_t coverindex) {
  char statetext[COVER_STATE_TEXT_MAXLEN];
  return GetTextIndexed(statetext, sizeof(statetext), coverStates[coverindex], kCoverStateText);
}

// return dummy position (0-100, HAss defalt style) based on state
uint8_t coverGetPosition(uint8_t coverindex) {
  return kCoverPositions[coverStates[coverindex]];
}

bool Xdrv40(uint8_t function)
{
  bool result = false;

  switch (function) {
    case FUNC_PRE_INIT:
      CoverInit();
      break;
    //case FUNC_EVERY_50_MSECOND:
    //case FUNC_EVERY_250_MSECOND:
    case FUNC_EVERY_SECOND:
      coverCheckState();
      result = true;
    case FUNC_COMMAND:
      result = DecodeCommand(kCoverCommands, CoverCommand);
      break;
    case FUNC_JSON_APPEND:
      for (uint8_t i = 0; i < COVER_MAXCOUNT; i++) {
        ResponseAppend_P(",");
        ResponseAppend_P(JSON_COVER_STATE, i+1, coverGetStateText(i), coverGetPosition(i));
      }
      break;
    break;
  }
  return result;    
}

// init on boot
void CoverInit(void)
{
  // on boot, target state is unknown
  for (uint32_t i = 0; i < COVER_MAXCOUNT) {
    coverTarget[i] = CS_UNKNOWN;
  }
}

// check state based on sensor input
void coverCheckState()
{
  for (uint32_t i = 0; i < COVER_MAXCOUNT; i++) {
    coverstate_t newstate = CS_UNKNOWN;
    if (Settings.cover_cfg[i].switch_obstruct)&&(SwitchState(Settings.cover_cfg[i].switch_obstruct-1)) {
      // obstruction sensor exists and ON
      newstate = CS_OBSTRUCTED;
    } else if (Settings.cover_cfg[i].switch_open) {
      if (SwitchState(Settings.cover_cfg[i].switch_open-1)) {
        // open sensor exists and ON
        newstate = CS_OPEN;
      } else if !(Settings.cover_cfg[i].switch_closed) {
        // open sensor exists, is OFF, and there is no closed switch - assume closed
        newstate = CS_CLOSED;
      }
    } else if (Settings.cover_cfg[i].switch_closed) {
      if (SwitchState(Settings.cover_cfg[i].switch_closed-1)) {
        // closed sensor exists and ON
        newstate = CS_CLOSED;
      } else if !(Settings.cover_cfg[i].switch_open) {
        // closed sensor exists, is OFF, and there is no open switch - assume open
        newstate = CS_OPEN;        
      }
    }
    if (newstate == CS_UNKNOWN) { // didn't get a state from sensor input
      switch (coverStates[i]): // check last known state
      case CS_CLOSED: // change from closed to unknown - probably opening
      case CS_OPENING: // was opening, probably still is
        newstate = CS_OPENING;
        break;
      case CS_OPEN: // change from open to unknown - probably closing
      case CS_CLOSING: // was closing, probably still is
        newstate = CS_CLOSING;
        break;
      case: CS_STOPPED:
        newstate = CS_STOPPED; // was stopped, probably still stopped
        break;
    }
    coverStates[i] = newstate; 
    
    // followup
    if (coverFollowUp[i] > 0) {
      coverFollowUp[i]--;
    }
    if (coverStates[i] == coverTarget[i]) {
      coverFollowUp[i] = 0;
    } else if (coverFollowUp[i] == 1) {
      // door hasn't gone where it should; hit the button again (followup only in toggle mode)
      ExecuteCommandPower(Settings.cover_cfg[i].relay_open, POWER_ON, SRC_IGNORE);
      coverFollowUp[i] = COVER_FOLLOWUPINTERVAL; // round again and again!
    }
  }
}

// test: if relay_open is assigned, but relay_close is not, then we consider relay_open to be a toggle relay
bool CoverIsToggleMode(uint8_t doorindex)
{
  return (Settings.cover_cfg[doorindex].relay_open)&&(!Settings.cover_cfg[doorindex].relay_close)
}


/*********************************************************************************************\
 * Commands
\*********************************************************************************************/

void CmndCoverOpen(void) {
  if !(XdrvMailbox.index) { XdrvMailbox.index = 1; };
  if ((XdrvMailbox.index <= COVER_MAXCOUNT)) {
    uint32_t doorindex = XdrvMailbox.index - 1;
    if (Settings.cover_cfg[doorindex].relay_open) && !(CoverIsToggleMode(doorindex) && ((coverStates[doorindex] == CS_OPEN) || (coverStates[doorindex] == CS_OPENING))) {
      ExecuteCommandPower(Settings.cover_cfg[doorindex].relay_open, POWER_ON, SRC_IGNORE);
      if CoverIsToggleMode(doorindex) {
        coverFollowUp = COVER_FOLLOWUPINTERVAL; // follow this one up...
      }
    } 
  }
}

void CmndCoverClose(void) {
  if !(XdrvMailbox.index) { XdrvMailbox.index = 1; };
  if ((XdrvMailbox.index <= COVER_MAXCOUNT)) {
    uint32_t doorindex = XdrvMailbox.index - 1;
    uint8_t relay = Settings.cover_cfg[doorindex].relay_close;
    if (CoverIsToggleMode(doorindex)) {
      if (coverStates[doorindex] == CS_CLOSED) || (coverStates[doorindex] == CS_CLOSING) {
        relay = 0;
      } else {
        relay = Settings.cover_cfg[doorindex].relay_open;
        coverFollowUp = COVER_FOLLOWUPINTERVAL; // follow this one up...
      }
    }
    if (relay) {
      ExecuteCommandPower(relay, POWER_ON, SRC_IGNORE);
      coverTarget[doorindex] = CS_CLOSED;
    }
  } 
}

void CmndCoverStop(void) {
  if !(XdrvMailbox.index) { XdrvMailbox.index = 1; };
  if ((XdrvMailbox.index <= COVER_MAXCOUNT)) { 
    uint32_t doorindex = XdrvMailbox.index - 1;
    uint8_t relay = 0;
    if (Settings.cover_cfg[doorindex].relay_stop) {
      relay = Settings.cover_cfg[doorindex].relay_stop;
    } else if (CoverIsToggleMode(doorindex)) {
      relay = Settings.cover_cfg[doorindex].relay_open;
    }
    if (relay) {
      ExecuteCommandPower(relay, POWER_ON, SRC_IGNORE);
      coverStates[doorindex] = CS_STOP; // optimistic for non-sensed state
    }
  }
}

void CmndCoverToggle(void) {
  if !(XdrvMailbox.index) { XdrvMailbox.index = 1; };
  if ((XdrvMailbox.index <= COVER_MAXCOUNT)) {
    uint32_t doorindex = XdrvMailbox.index - 1;
    switch coverStates[doorindex]:
      case CS_OPEN:
      case CS_OPENING:
        CmndCoverClose();
        break;
      case CS_CLOSED:
      case CS_CLOSING:
        CmndCoverOpen();
        break;
      case CS_STOPPED:
      case CS_UNKNOWN:
      case CS_OBSTRUCTED:
        // toggle based on target; otherwise close
        if coverTarget[doorindex] == CS_CLOSED {
          CmndCoverClose();
        } else {
          CmndCoverClose();
        break;          
  }
}


// CoverRelays<index> <open>, <close>, <obstruct>
// e.g. CoverRelays1 1, 0, 0
void CmndCoverRelays(void) {
  if !(XdrvMailbox.index) { XdrvMailbox.index = 1; };
  if ((XdrvMailbox.index <= COVER_MAXCOUNT)) {
    uint32_t doorindex = XdrvMailbox.index - 1;
    uint32_t params[3];
    uint32_t pcount = ParseParameters(3, params);
    if (pcount > 0) {
      Settings.cover_cfg[doorindex].relay_open = params[0];
    }
    if (pcount > 1) {
      Settings.cover_cfg[doorindex].relay_close = params[1];
    }
    if (pcount > 2) {
      Settings.cover_cfg[doorindex].relay_stop = params[2];
    }
    // response example: {"CoverRelays1":{"Open":0,"Close":1,"Stop":2}}
    Response_P(S_JSON_COMMAND_COVER_RELAYS, XdrvMailbox.index, Settings.cover_cfg[doorindex].relay_open, Settings.cover_cfg[doorindex].relay_close, Settings.cover_cfg[doorindex].relay_stop);
  }
}

void CmndCoverSwitches(void) {
  if !(XdrvMailbox.index) { XdrvMailbox.index = 1; };
  if ((XdrvMailbox.index <= COVER_MAXCOUNT)) {
    uint32_t doorindex = XdrvMailbox.index - 1;
    uint32_t params[3];
    uint32_t pcount = ParseParameters(3, params);
    if (pcount > 0) {
      Settings.cover_cfg[doorindex].switch_open = params[0];
    }
    if (pcount > 1) {
      Settings.cover_cfg[doorindex].switch_closed = params[1];
    }
    if (pcount > 2) {
      Settings.cover_cfg[doorindex].switch_obstruct = params[2];
    }
    // response example: {"CoverRelays1":{"Open":0,"Close":1,"Stop":2}}
    Response_P(S_JSON_COMMAND_COVER_SWITCHES, XdrvMailbox.index, Settings.cover_cfg[doorindex].switch_open, Settings.cover_cfg[doorindex].switch_closed, Settings.cover_cfg[doorindex].switch_obstruct);
  }
}

void CmndCoverState(void) {
  
}

#endif //USE_DOORCONTROL