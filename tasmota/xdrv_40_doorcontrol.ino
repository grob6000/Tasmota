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

#define DOOR_MAXCOUNT 2

enum DoorState { CLOSED, OPEN, CLOSING, OPENING, STOPPED, OBSTRUCTED, UNKNOWN };
enum DoorCmnd { CLOSE, OPEN, STOP };

// init on boot
void DoorInit(void)
{
  
}

void CmndDoor(DoorCmnd cmnd, uint8_t doorindex)
{
  
}

DoorState GetDoorState(uint8_t index)
{
  
}

#endif //USE_DOORCONTROL