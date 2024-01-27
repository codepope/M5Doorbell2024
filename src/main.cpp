#include "RF433any.h"
#include <Arduino.h>
#include "M5Atom.h"
#include <WiFi.h>
#include "Pushsafer.h"
#include <WifiClient.h>
#include "secrets.h"
#include <Preferences.h>

#define PIN_RFINPUT 22

// #define EEPROM_SIZE 16 // one byte for index, 3 bytes per button
Preferences preferences;

WiFiClient client;
Pushsafer pushsafer(SECRET_PUSHSAFERKEY, client);

int matchValueIndoors = SECRET_INDOOR;
int matchValueFrontdoor = SECRET_FRONTDOOR;

const char ssid[] = SECRET_WIFISSID;
const char password[] = SECRET_WIFIKEY;

Track track(PIN_RFINPUT);

int i = 0;

void callback_generic(const BitVector *recorded)
{
  Serial.print(F("Code received: "));
  char *printed_code = recorded->to_str();

  if (printed_code)
  {
    Serial.print(recorded->get_nb_bits());
    Serial.print(F(" bits: ["));
    Serial.print(printed_code);
    Serial.print(F("]\n"));

    free(printed_code);
  }
}

int buttoncount = 0;

struct ButtonState
{
  byte code[3];
  time_t lastPressed;
};

ButtonState buttons[4];

void setup()
{
  Serial.begin(115200);
  M5.begin(true, false, true);

  M5.update();

  preferences.begin("doorbell", false);

  if (M5.Btn.isPressed())
  {
    Serial.println("Resetting preferences");
    preferences.clear();
    preferences.putInt("buttoncount", 0);
  }

  buttoncount = preferences.getInt("buttoncount", 0);

  Serial.print("Button count: ");
  Serial.println(buttoncount);

  for (int i = 0; i < buttoncount; i++)
  {
    char key[10];
    sprintf(key, "button%d", i);
    preferences.getBytes(key, buttons[i].code, 3);
    Serial.print("Button ");
    Serial.print(i);
    Serial.print(" code: ");
    Serial.print(buttons[i].code[0]);
    Serial.print(" ");
    Serial.print(buttons[i].code[1]);
    Serial.print(" ");
    Serial.println(buttons[i].code[2]);
    buttons[i].lastPressed = time(nullptr) - 10;
  }

  delay(50);
  M5.dis.fillpix(0x000000);

  // Set WiFi to station mode and disconnect from an AP if it was Previously
  // connected
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  WiFi.begin(ssid, password);

  delay(100);

  bool flip = true;
  int retryIntervalMs = 500;
  int wifiClientConnectionTimeoutSeconds = 10;
  int timeoutCounter = wifiClientConnectionTimeoutSeconds * (1000 / retryIntervalMs);

  while (WiFi.status() != WL_CONNECTED && timeoutCounter > 0)
  {
    delay(retryIntervalMs);
    if (timeoutCounter == (wifiClientConnectionTimeoutSeconds * 2 - 3))
    {
      Serial.println("Retrying");
      WiFi.reconnect();
    }
    timeoutCounter--;
    if (flip)
    {
      M5.dis.fillpix(0xff0000);
    }
    else
    {
      M5.dis.fillpix(0x000000);
    }
    flip = !flip;
  }

  M5.dis.fillpix(0x0ff00);
  delay(1000);
  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  pushsafer.debug = 9;
}

void loop()
{
  track.treset();

  while (!track.do_events())
    delay(1);

  Decoder *pdec0 = track.get_data(
      RF433ANY_FD_DECODED | RF433ANY_FD_DEDUP | RF433ANY_FD_NO_ERROR);

  int muteignore = 0;

  for (Decoder *pdec = pdec0; pdec != nullptr; pdec = pdec->get_next())
  {
    char *buf = pdec->get_pdata()->to_str();

    {
      if (pdec->get_nb_bits() == 24 && pdec->get_repeats() == 3)
      {
        // Serial.print("Received ");
        // Serial.print(pdec->get_nb_bits());
        // Serial.print(" bits: [");
        // Serial.print(buf);
        // Serial.print("] ");

        bool found = false;
        for (int i = 0; i < buttoncount; i++)
        {
          if (buttons[i].code[0] == pdec->get_pdata()->get_nth_byte(0) &&
              buttons[i].code[1] == pdec->get_pdata()->get_nth_byte(1) &&
              buttons[i].code[2] == pdec->get_pdata()->get_nth_byte(2))
          {
            if (buttons[i].lastPressed + 5 < time(nullptr))
            {
              Serial.printf("Button %d - sending push notification\n", i);
              struct PushSaferInput input;
              input.title = "Ding Dong";
              input.message = "There is someone at the door";
              input.sound = 1;
              input.vibration = 3;
              input.icon = "1";
              input.device = "a";
              input.url = "";
              input.urlTitle = "";
              input.picture = "";
              input.picture2 = "";
              input.picture3 = "";
              input.time2live = "";
              input.retry = "";
              input.expire = "";
              input.answer = "";
              Serial.println("Sending");
              Serial.println(pushsafer.sendEvent(input));
              Serial.println("Sent");
              Serial.println(WiFi.status());
              if(WiFi.status() != WL_CONNECTED)
              {
                Serial.println("Reconnecting");
                WiFi.reconnect();
              }
              buttons[i].lastPressed = time(nullptr);
              muteignore = 10;
            }
            else
            {
              if (muteignore > 0)
              {
                muteignore--;
              }
              else
              {
                Serial.printf("Button %d - ignoring\n", i);
              }
            }
            found = true;
          }
        }

        if (!found)
        {
          M5.update(); // You need to add m5.update () to read the state of the key, see System for details
          if (M5.Btn.isPressed())
          {
            Serial.println("Button pressed, adding to EEPROM");
            if (buttoncount < 4)
            {
              buttons[buttoncount].code[0] = pdec->get_pdata()->get_nth_byte(0);
              buttons[buttoncount].code[1] = pdec->get_pdata()->get_nth_byte(1);
              buttons[buttoncount].code[2] = pdec->get_pdata()->get_nth_byte(2);
              buttons[buttoncount].lastPressed = 0;
              buttoncount++;
              preferences.putInt("buttoncount", buttoncount);
              char key[10];
              sprintf(key, "button%d", buttoncount - 1);
              preferences.putBytes(key, buttons[buttoncount - 1].code, 3);
            }
            else
            {
              Serial.println("Too many buttons");
            }
          }
          else
          {
            Serial.println("Button not known or saved");
          }
        }
      }
      free(buf);
    }
  }
  delete pdec0;
}
