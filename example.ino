/// This is not a complete example, only snippets of code

#include "Adafruit_BLE.h"
#include "Adafruit_BluefruitLE_SPI.h"
#include "Adafruit_BluefruitLE_UART.h"
#include "BluefruitConfig.h"


// BLE
Adafruit_BluefruitLE_SPI ble(BLUEFRUIT_SPI_CS, BLUEFRUIT_SPI_IRQ, BLUEFRUIT_SPI_RST);
int32_t bleMidiServiceId;
int32_t bleMidiCharId;

// BLE MIDI parsing
enum BLE_HANDLER_STATE
{
  BLE_HEADER = 0,
  BLE_TIMESTAMP,
  BLE_STATUS,
  BLE_STATUS_RUNNING,
  BLE_PARAMS,
  BLE_SYSTEM_RT,
  BLE_SYSEX,
  BLE_SYSEX_END,
  BLE_SYSEX_INT
};

BLE_HANDLER_STATE bleHandlerState = BLE_HEADER;

uint16_t sysExBufferPos;
uint8_t sysExBuffer[128];
uint16_t timestamp;
uint8_t tsHigh;
uint8_t tsLow;
uint8_t bleMidiBuffer[3];
uint8_t bleMidiBufferPos = 0;
bool bleSysExHasFinished = true;

void setup() {
  // BLE
  Serial.print(F("Initializing Bluetooth..."));

  SPI1.setMISO(1);
  SPI1.setMOSI(0);
  SPI1.setSCK(32);

  delay(50);

  if (!ble.begin(VERBOSE_MODE))
  {
    onError((char *)F("Couldn't find Bluefruit, make sure it's in Command mode & check wiring?"));
    return;
  }


  delay(200);

  Serial.println(F("Performing a factory reset: "));
  if (!ble.factoryReset())
  {
    onError((char *)F("Couldn't factory reset"));
    return;
  }
  // ble.echo(false);

  /* Change the device name to make it easier to find */
  if (!ble.sendCommandCheckOK(F("AT+GAPDEVNAME=Your BLE device")))
  {
    onError((char *)F("Could not set device name?"));
    return;
  }


  /* Set up BLE MIDI service*/
  Serial.print(F("Clear Gatt "));
  if (!ble.sendCommandCheckOK(F("AT+GATTCLEAR")))
  {
    onError((char *)F("Failed to clear GATT"));
    return;
  }


  Serial.print(F("Add MIDI Service "));
  if (!ble.sendCommandWithIntReply(F("AT+GATTADDSERVICE=UUID128=03-B8-0E-5A-ED-E8-4B-33-A7-51-6C-E3-4E-C4-C7-00"), &bleMidiServiceId))
  {
    onError((char *)F("Failed to add BLE MIDI Service"));
    return;
  }


  Serial.print(F("Add MIDI Characteristic "));
  if (!ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID128=77-72-E5-DB-38-68-41-12-A1-A9-F2-66-9D-10-6B-F3,PROPERTIES=0x16,MIN_LEN=1,MAX_LEN=20,VALUE=0,DATATYPE=2,DESCRIPTION=MIDI,PRESENTATION=17-00-00-27-01-00-00"), &bleMidiCharId))
  {
    onError((char *)F("Failed to add BLE MIDI Characteristic"));
    return;
  }

  // ble.sendCommandCheckOK(F("AT+GATTLIST"));

  Serial.print(F("Adding MIDI Service UUID to the advertising payload: "));
  if (!ble.sendCommandCheckOK(F("AT+GAPSETADVDATA=02-01-06-11-07-00-C7-C4-4E-E3-6C-51-A7-33-4B-E8-ED-5A-0E-B8-03")))
  {
    onError((char *)F("Failed to set BLE ADV DATA"));
    return;
  }

  Serial.println(F("Reset ble"));
  ble.reset();

  delay(200);

  /* Set BLE callbacks */
  ble.setConnectCallback(onBLEMidiConnected);
  ble.setDisconnectCallback(onBLEMidiDisconnected);
  ble.setBleGattRxCallback(bleMidiCharId, onBleGattRX);

  ble.verbose(false); // debug info is a little annoying after this point!
}



void onBleGattRX(int32_t chars_id, uint8_t data[], uint16_t len)
{
  if (chars_id == bleMidiCharId)
  {
    parseBLEMidiPackage(data, len);
  }
}

// BLE MIDI Processing -----------------------------------------------------------

void parseBLEMidiPackage(uint8_t *rxBuffer, size_t length)
{
  if (length > 1)
  {
    // parse BLE message
    bleHandlerState = BLE_HEADER;

    uint8_t header = rxBuffer[0];
    uint8_t statusByte = 0;

    // Serial.print(F("bleHeader "));
    // Serial.println(header);

    for (size_t i = 1; i < length; i++)
    {
      uint8_t midiByte = rxBuffer[i];
      // Serial.print(F("bleHandlerState "));
      // Serial.print(bleHandlerState);

      // State handling
      switch (bleHandlerState)
      {
      case BLE_HEADER:
        if (!bleSysExHasFinished)
        {
          if ((midiByte & 0x80) == 0x80)
          { // System messages can interrupt ongoing sysex
            // bleHandlerState = BLE_TIMESTAMP;
            bleHandlerState = BLE_SYSEX_INT;
          }
          else
          {
            // Sysex continue
            bleHandlerState = BLE_SYSEX;
          }
        }
        else
        {
          bleHandlerState = BLE_TIMESTAMP;
        }
        break;

      case BLE_TIMESTAMP:
        if ((midiByte & 0xFF) == 0xF0)
        { // Sysex start
          bleSysExHasFinished = false;
          sysExBufferPos = 0;
          bleHandlerState = BLE_SYSEX;
        }
        else if ((midiByte & 0x80) == 0x80)
        { // Status/System start
          bleHandlerState = BLE_STATUS;
        }
        else
        {
          bleHandlerState = BLE_STATUS_RUNNING;
        }
        break;

      case BLE_STATUS:
        if ((midiByte & 0x80) == 0x80)
        { // If theres a timestamp after a status, it must have been a single byte real time message
          bleHandlerState = BLE_TIMESTAMP;
        }
        else
        {
          bleHandlerState = BLE_PARAMS;
        }
        break;

      case BLE_STATUS_RUNNING:
        bleHandlerState = BLE_PARAMS;
        break;

      case BLE_PARAMS: // After params can come TSlow or more params
        if ((midiByte & 0x80) == 0x80)
        {
          bleHandlerState = BLE_TIMESTAMP;
        }
        break;

      case BLE_SYSEX:
        if ((midiByte & 0x80) == 0x80)
        { // Sysex end and interrupting RT bytes is preceded with a TSLow
          bleHandlerState = BLE_SYSEX_INT;
        }
        break;

      case BLE_SYSEX_INT:
        if ((midiByte & 0xF7) == 0xF7)
        { // Sysex end
          bleSysExHasFinished = true;
          bleHandlerState = BLE_SYSEX_END;
        }
        else
        {
          bleHandlerState = BLE_SYSTEM_RT;
        }
        break;

      case BLE_SYSTEM_RT:
        if (!bleSysExHasFinished)
        { // Continue incomplete Sysex
          bleHandlerState = BLE_SYSEX;
        }
        else
        {
          bleHandlerState = BLE_TIMESTAMP; // Always a timestamp after a realtime message
        }
        break;

      default:
        Serial.print(F("Unhandled state "));
        Serial.println(bleHandlerState);
        break;
      }

      // Serial.print(F(" -> "));
      Serial.print(bleHandlerState);
      // Serial.print(F(" - midiByte "));
      Serial.print(F(" - "));
      Serial.println(midiByte, HEX);

      // Data handling
      switch (bleHandlerState)
      {
      case BLE_TIMESTAMP:
        // Serial.println(F("set timestamp"));
        tsHigh = header & 0x3f;
        tsLow = midiByte & 0x7f;
        timestamp = (tsHigh << 7) | tsLow;
        break;

      case BLE_STATUS:
        // Serial.println(F("set status"));
        statusByte = midiByte;
        bleMidiBufferPos = 0;
        bleMidiBuffer[bleMidiBufferPos] = statusByte;
        processMessageOfType(statusByte);
        break;

      case BLE_STATUS_RUNNING:
        // Serial.println(F("set running status"));
        bleMidiBufferPos = 0;
        bleMidiBuffer[bleMidiBufferPos] = statusByte;
        processMessageOfType(statusByte);
        break;

      case BLE_PARAMS:
        // Serial.print(F("add param "));
        // Serial.println(midiByte, HEX);
        bleMidiBuffer[++bleMidiBufferPos] = midiByte;
        processMessageOfType(statusByte);
        break;

      case BLE_SYSTEM_RT:
        // Serial.println(F("handle RT"));
        bleMidiBufferPos = 0;
        bleMidiBuffer[bleMidiBufferPos] = midiByte;
        processMessageOfType(midiByte);
        break;

      case BLE_SYSEX:
        // Serial.println(F("add sysex"));
        addToSysexBuffer(midiByte);
        break;

      case BLE_SYSEX_INT:
        // Serial.println(F("add sysex int"));
        addToSysexBuffer(midiByte);
        break;

      case BLE_SYSEX_END:
        // Serial.println(F("finalize sysex"));
        finalizeSysexBuffer(midiByte);
        break;

      default:
        Serial.print(F("Unhandled state (data)"));
        Serial.println(bleHandlerState);
        break;
      }
    }
  }
}

void processMessageOfType(uint8_t type)
{
  // Serial.print(F("Process type ")); Serial.println(type, HEX);
  // Serial.print(F("Buffer pos ")); Serial.println(bleMidiBufferPos);
  if (bleMidiBufferPos == 0)
  {
    switch (type)
    {
    case 0xF6:
      onTuneRequest();
      break;
    case 0xF8:
      onClock();
      break;
    case 0xFA:
      onStart();
      break;
    case 0xFB:
      onContinue();
      break;
    case 0xFC:
      onStop();
      break;
    case 0xFF:
      onActiveSensing();
      break;
    case 0xFE:
      onSystemReset();
      break;
    }
  }
  else
  {
    uint8_t channel = type & 0xF;
    uint8_t midiType = type & 0xF0;

    // Serial.print(F("Process miditype ")); Serial.println(midiType, HEX);

    if (bleMidiBufferPos == 1)
    {
      if (type == 0xF1)
      {
        onTimeCodeQuarterFrame(bleMidiBuffer[1]);
      }
      else if (type == 0xF3)
      {
        onSongSelect(bleMidiBuffer[1]);
      }
      else if (midiType == 0xC0)
      {
        onProgramChange(channel, bleMidiBuffer[1]);
      }
      else if (midiType == 0xD0)
      {
        onAfterTouchChannel(channel, bleMidiBuffer[1]);
      }
    }
    else if (bleMidiBufferPos == 2)
    {
      if (type == 0xF2)
      {
        onSongPosition(bleMidiBuffer[1] + (bleMidiBuffer[2] << 7));
      }
      else if (midiType == 0x80)
      {
        onNoteOff(channel, bleMidiBuffer[1], bleMidiBuffer[2]);
      }
      else if (midiType == 0x90)
      {
        onNoteOn(channel, bleMidiBuffer[1], bleMidiBuffer[2]);
      }
      else if (midiType == 0xA0)
      {
        onAfterTouchPoly(channel, bleMidiBuffer[1], bleMidiBuffer[2]);
      }
      else if (midiType == 0xB0)
      {
        onControlChange(channel, bleMidiBuffer[1], bleMidiBuffer[2]);
      }
      else if (midiType == 0xE0)
      {
        onPitchChange(channel, bleMidiBuffer[1] + (bleMidiBuffer[2] << 7) - 8192);
      }
    }
  }
}

void addToSysexBuffer(uint8_t sysexByte)
{
  if (sysExBufferPos == 128)
  {
    onSystemExclusiveChunk(sysExBuffer, sysExBufferPos, false);
    sysExBufferPos = 0;
  }
  sysExBuffer[sysExBufferPos++] = sysexByte;
}

void finalizeSysexBuffer(uint8_t sysexByte)
{
  if (sysExBufferPos > 0)
  {
    sysExBuffer[sysExBufferPos - 1] = sysexByte;
    onSystemExclusiveChunk(sysExBuffer, sysExBufferPos, true);
  }
}
