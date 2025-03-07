#include "../ESPEasyCore/ESPEasyWifi.h"

#include "../../ESPEasy-Globals.h"
#include "../DataStructs/TimingStats.h"
#include "../ESPEasyCore/ESPEasyNetwork.h"
#include "../ESPEasyCore/ESPEasyWiFiEvent.h"
#include "../ESPEasyCore/ESPEasyWifi_ProcessEvent.h"
#include "../ESPEasyCore/ESPEasy_Log.h"
#include "../ESPEasyCore/Serial.h"
#include "../Globals/ESPEasyWiFiEvent.h"
#include "../Globals/EventQueue.h"
#include "../Globals/NetworkState.h"
#include "../Globals/Nodes.h"
#include "../Globals/RTC.h"
#include "../Globals/SecuritySettings.h"
#include "../Globals/Services.h"
#include "../Globals/Settings.h"
#include "../Globals/WiFi_AP_Candidates.h"
#include "../Helpers/ESPEasy_time_calc.h"
#include "../Helpers/Hardware_defines.h"
#include "../Helpers/Misc.h"
#include "../Helpers/Networking.h"
#include "../Helpers/StringConverter.h"
#include "../Helpers/StringGenerator_WiFi.h"
#include "../Helpers/StringProvider.h"

#ifdef ESP32
#include <WiFiGeneric.h>
#include <esp_wifi.h> // Needed to call ESP-IDF functions like esp_wifi_....

#include <esp_phy_init.h>
#endif

// FIXME TD-er: Cleanup of WiFi code
#ifdef ESPEASY_WIFI_CLEANUP_WORK_IN_PROGRESS
bool ESPEasyWiFi_t::begin() {
  return true;
}

void ESPEasyWiFi_t::end() {


}


void ESPEasyWiFi_t::loop() {
  switch (_state) {
    case WiFiState_e::OFF:
    break;
    case WiFiState_e::AP_only:
    break;
    case WiFiState_e::ErrorRecovery:
    // Wait for timeout to expire
    // Start again from scratch
    break;
    case WiFiState_e::STA_Scanning:
    case WiFiState_e::STA_AP_Scanning:
    // Check if scanning is finished
    // When scanning per channel, call for scanning next channel
    break;
    case WiFiState_e::STA_Connecting:
    case WiFiState_e::STA_Reconnecting:
    // Check if (re)connecting has finished
    break;
    case WiFiState_e::STA_Connected:
    // Check if still connected
    // Reconnect if not.
    // Else mark last timestamp seen as connected
    break;
  }


  {
    // Check if we need to start AP
    // Flag captive portal in webserver and/or whether we might be in setup mode
  }

#ifdef USE_IMPROV
  {
    // Check for Improv mode.
  }
#endif


}


IPAddress  ESPEasyWiFi_t::getIP() const {

  IPAddress res;


  return res;
}

void  ESPEasyWiFi_t::disconnect() {

}


void ESPEasyWiFi_t::checkConnectProgress() {

}

void ESPEasyWiFi_t::startScanning() {
  _state = WiFiState_e::STA_Scanning;
  WifiScan(true);
  _last_state_change.setNow();
}


bool ESPEasyWiFi_t::connectSTA() {
  if (!WiFi_AP_Candidates.hasCandidateCredentials()) {
    if (!WiFiEventData.warnedNoValidWiFiSettings) {
      addLog(LOG_LEVEL_ERROR, F("WIFI : No valid wifi settings"));
      WiFiEventData.warnedNoValidWiFiSettings = true;
    }
    WiFiEventData.last_wifi_connect_attempt_moment.clear();
    WiFiEventData.wifi_connect_attempt     = 1;
    WiFiEventData.wifiConnectAttemptNeeded = false;

    // No need to wait longer to start AP mode.
    if (!Settings.DoNotStartAP()) {
      setAP(true);
    }
    return false;
  }
  WiFiEventData.warnedNoValidWiFiSettings = false;
  setSTA(true);
  #if defined(ESP8266)
  wifi_station_set_hostname(NetworkCreateRFCCompliantHostname().c_str());

  #endif // if defined(ESP8266)
  #if defined(ESP32)
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  #endif // if defined(ESP32)
  setConnectionSpeed();
  setupStaticIPconfig();



    // Start the process of connecting or starting AP
    if (WiFi_AP_Candidates.getNext(true)) {
      // Try to connect to AP

    } else {
      // No (known) AP, start scanning
      startScanning();
    }


  return true;
}

#endif // ESPEASY_WIFI_CLEANUP_WORK_IN_PROGRESS


// ********************************************************************************
// WiFi state
// ********************************************************************************

/*
   WiFi STA states:
   1 STA off                 => ESPEASY_WIFI_DISCONNECTED
   2 STA connecting
   3 STA connected           => ESPEASY_WIFI_CONNECTED
   4 STA got IP              => ESPEASY_WIFI_GOT_IP
   5 STA connected && got IP => ESPEASY_WIFI_SERVICES_INITIALIZED

   N.B. the states are flags, meaning both "connected" and "got IP" must be set
        to be considered ESPEASY_WIFI_SERVICES_INITIALIZED

   The flag wifiConnectAttemptNeeded indicates whether a new connect attempt is needed.
   This is set to true when:
   - Security settings have been saved with AP mode enabled. FIXME TD-er, this may not be the best check.
   - WiFi connect timeout reached  &  No client is connected to the AP mode of the node.
   - Wifi is reset
   - WiFi setup page has been loaded with SSID/pass values.


   WiFi AP mode states:
   1 AP on                        => reset AP disable timer
   2 AP client connect/disconnect => reset AP disable timer
   3 AP off                       => AP disable timer = 0;

   AP mode will be disabled when both apply:
   - AP disable timer (timerAPoff) expired
   - No client is connected to the AP.

   AP mode will be enabled when at least one applies:
   - No valid WiFi settings
   - Start AP timer (timerAPstart) expired

   Start AP timer is set or cleared at:
   - Set timerAPstart when "valid WiFi connection" state is observed.
   - Disable timerAPstart when ESPEASY_WIFI_SERVICES_INITIALIZED wifi state is reached.

   For the first attempt to connect after a cold boot (RTC values are 0), a WiFi scan will be 
   performed to find the strongest known SSID.
   This will set RTC.lastBSSID and RTC.lastWiFiChannel
   
   Quick reconnect (using BSSID/channel of last connection) when both apply:
   - If wifi_connect_attempt < 3
   - RTC.lastBSSID is known
   - RTC.lastWiFiChannel != 0

   Change of wifi settings when both apply:
   - "other" settings valid
   - (wifi_connect_attempt % 2) == 0

   Reset of wifi_connect_attempt to 0 when both apply:
   - connection successful
   - Connection stable (connected for > 5 minutes)

 */


// ********************************************************************************
// Check WiFi connected status
// This is basically the state machine to switch between states:
// - Initiate WiFi reconnect
// - Start/stop of AP mode
// ********************************************************************************
bool WiFiConnected() {
  START_TIMER;

  static bool recursiveCall = false;

  static uint32_t lastCheckedTime = 0;
  static bool lastState = false;

#if FEATURE_USE_IPV6
  if (!WiFiEventData.processedGotIP6) {
    processGotIPv6();
  }
#endif

  if (!WifiIsSTA(WiFi.getMode())) {
    lastState = false;
    return lastState;
  }


  if (lastCheckedTime != 0 && timePassedSince(lastCheckedTime) < 100) {
    if (WiFiEventData.lastDisconnectMoment.isSet() &&
        WiFiEventData.lastDisconnectMoment.millisPassedSince() > timePassedSince(lastCheckedTime))
    {
      // Try to rate-limit the nr of calls to this function or else it will be called 1000's of times a second.
      return lastState;
    }
  }



  if (WiFiEventData.unprocessedWifiEvents()) { return false; }

  bool wifi_isconnected = WiFi.isConnected();
  #ifdef ESP8266
  // Perform check on SDK function, see: https://github.com/esp8266/Arduino/issues/7432
  station_status_t status = wifi_station_get_connect_status();
  switch(status) {
    case STATION_GOT_IP:
      wifi_isconnected = true;
      break;
    case STATION_NO_AP_FOUND:
    case STATION_CONNECT_FAIL:
    case STATION_WRONG_PASSWORD:
      wifi_isconnected = false;
      break;
    case STATION_IDLE:
    case STATION_CONNECTING:
      break;

    default:
      wifi_isconnected = false;
      break;
  }
  #endif

  if (recursiveCall) return wifi_isconnected;
  recursiveCall = true;


  // For ESP82xx, do not rely on WiFi.status() with event based wifi.
  const int32_t wifi_rssi = WiFi.RSSI();
  bool validWiFi = (wifi_rssi < 0) && wifi_isconnected && hasIPaddr();
  /*
  if (validWiFi && WiFi.channel() != WiFiEventData.usedChannel) {
    validWiFi = false;
  }
  */
  if (validWiFi != WiFiEventData.WiFiServicesInitialized()) {
    // else wifiStatus is no longer in sync.
    if (checkAndResetWiFi()) {
      // Wifi has been reset, so no longer valid WiFi
      validWiFi = false;
    }
  }

  if (validWiFi) {
    // Connected, thus disable any timer to start AP mode. (except when in WiFi setup mode)
    if (!WiFiEventData.wifiSetupConnect) {
      WiFiEventData.timerAPstart.clear();
    }
    STOP_TIMER(WIFI_ISCONNECTED_STATS);
    recursiveCall = false;
    // Only return true after some time since it got connected.
#if FEATURE_SET_WIFI_TX_PWR
    SetWiFiTXpower();
#endif
    lastState = WiFiEventData.wifi_considered_stable || WiFiEventData.lastConnectMoment.timeoutReached(100);
    lastCheckedTime = millis();
    return lastState;
  }

  if ((WiFiEventData.timerAPstart.isSet()) && WiFiEventData.timerAPstart.timeReached()) {
    if (WiFiEventData.timerAPoff.isSet() && !WiFiEventData.timerAPoff.timeReached()) {
      if (!Settings.DoNotStartAP()) {
        // Timer reached, so enable AP mode.
        if (!WifiIsAP(WiFi.getMode())) {
          if (!WiFiEventData.wifiConnectAttemptNeeded) {
            addLog(LOG_LEVEL_INFO, F("WiFi : WiFiConnected(), start AP"));
            WifiScan(false);
            setSTA(false); // Force reset WiFi + reduce power consumption
            setAP(true);
          }
        }
      }
    } else {
      WiFiEventData.timerAPstart.clear();
      WiFiEventData.timerAPoff.clear();
    }
  }


  // When made this far in the code, we apparently do not have valid WiFi connection.
  if (!WiFiEventData.timerAPstart.isSet() && !WifiIsAP(WiFi.getMode())) {
    // First run we do not have WiFi connection any more, set timer to start AP mode
    // Only allow the automatic AP mode in the first N minutes after boot.
    if (getUptimeMinutes() < WIFI_ALLOW_AP_AFTERBOOT_PERIOD) {
      WiFiEventData.timerAPstart.setMillisFromNow(WIFI_RECONNECT_WAIT);
      // Fixme TD-er: Make this more elegant as it now needs to know about the extra time needed for the AP start timer.
      WiFiEventData.timerAPoff.setMillisFromNow(WIFI_RECONNECT_WAIT + WIFI_AP_OFF_TIMER_DURATION);
    }
  }

  const bool timeoutReached = WiFiEventData.last_wifi_connect_attempt_moment.isSet() && 
                              WiFiEventData.last_wifi_connect_attempt_moment.timeoutReached(2 * DEFAULT_WIFI_CONNECTION_TIMEOUT);

  if (timeoutReached && !WiFiEventData.wifiSetup) {
    // It took too long to make a connection, set flag we need to try again
    //if (!wifiAPmodeActivelyUsed()) {
      WiFiEventData.wifiConnectAttemptNeeded = true;
    //}
    WiFiEventData.wifiConnectInProgress = false;
    if (!WiFiEventData.WiFiDisconnected()) {
      # ifndef BUILD_NO_DEBUG
      addLog(LOG_LEVEL_INFO, F("WiFi : wifiConnectTimeoutReached"));
      #endif
      WifiDisconnect();
    }
  }
  delay(0);
  STOP_TIMER(WIFI_NOTCONNECTED_STATS);
  recursiveCall = false;
  return false;
}

void WiFiConnectRelaxed() {
  if (!WiFiEventData.WiFiConnectAllowed() || WiFiEventData.wifiConnectInProgress) {
    if (WiFiEventData.wifiConnectInProgress) {
      if (WiFiEventData.last_wifi_connect_attempt_moment.isSet()) { 
        if (WiFiEventData.last_wifi_connect_attempt_moment.timeoutReached(WIFI_PROCESS_EVENTS_TIMEOUT)) {
          WiFiEventData.wifiConnectInProgress = false;
        }
      }
    }

    if (WiFiEventData.wifiConnectInProgress) {
      return; // already connected or connect attempt in progress need to disconnect first
    }
  }
  if (!WiFiEventData.processedScanDone) {
    // Scan is still active, so do not yet connect.
    return;
  }

  if (WiFiEventData.unprocessedWifiEvents()) {
    # ifndef BUILD_NO_DEBUG
    if (loglevelActiveFor(LOG_LEVEL_ERROR)) {
      String log = F("WiFi : Connecting not possible, unprocessed WiFi events: ");
      if (!WiFiEventData.processedConnect) {
        log += F(" conn");
      }
      if (!WiFiEventData.processedDisconnect) {
        log += F(" disconn");
      }
      if (!WiFiEventData.processedGotIP) {
        log += F(" gotIP");
      }
#if FEATURE_USE_IPV6
      if (!WiFiEventData.processedGotIP6) {
        log += F(" gotIP6");
      }
#endif

      if (!WiFiEventData.processedDHCPTimeout) {
        log += F(" DHCP_t/o");
      }
      
      addLogMove(LOG_LEVEL_ERROR, log);
      logConnectionStatus();
    }
    #endif
    return;
  }

  if (!WiFiEventData.wifiSetupConnect && wifiAPmodeActivelyUsed()) {
    return;
  }


  // FIXME TD-er: Should not try to prepare when a scan is still busy.
  // This is a logic error which may lead to strange issues if some kind of timeout happens and/or RF calibration was not OK.
  // Split this function into separate parts, with the last part being the actual connect attempt either after a scan is complete or quick connect is possible.

  AttemptWiFiConnect();
}

void AttemptWiFiConnect() {
  if (!WiFiEventData.wifiConnectAttemptNeeded) {
    return;
  }

  if (WiFiEventData.wifiConnectInProgress) {
    return;
  }

  setNetworkMedium(NetworkMedium_t::WIFI);
  if (active_network_medium != NetworkMedium_t::WIFI) 
  {
    return;
  }


  if (WiFiEventData.wifiSetupConnect) {
    // wifiSetupConnect is when run from the setup page.
    RTC.clearLastWiFi(); // Force slow connect
    WiFiEventData.wifi_connect_attempt = 0;
    WiFiEventData.wifiSetupConnect     = false;
    if (WiFiEventData.timerAPoff.isSet()) {
      WiFiEventData.timerAPoff.setMillisFromNow(WIFI_RECONNECT_WAIT + WIFI_AP_OFF_TIMER_DURATION);
    }
  }

  if (WiFiEventData.last_wifi_connect_attempt_moment.isSet()) {
    if (!WiFiEventData.last_wifi_connect_attempt_moment.timeoutReached(DEFAULT_WIFI_CONNECTION_TIMEOUT)) {
      return;
    }
  }

  if (WiFiEventData.unprocessedWifiEvents()) {
    return;
  }

  setSTA(true);

  if (WiFi_AP_Candidates.getNext(WiFiScanAllowed())) {
    const WiFi_AP_Candidate candidate = WiFi_AP_Candidates.getCurrent();

    if (loglevelActiveFor(LOG_LEVEL_INFO)) {
      addLogMove(LOG_LEVEL_INFO, strformat(
        F("WIFI : Connecting %s attempt #%u"),
        candidate.toString().c_str(),
        WiFiEventData.wifi_connect_attempt));
    }
    WiFiEventData.markWiFiBegin();
    if (prepareWiFi()) {
      setNetworkMedium(NetworkMedium_t::WIFI);
      RTC.clearLastWiFi();
      RTC.lastWiFiSettingsIndex = candidate.index;
      
      float tx_pwr = 0; // Will be set higher based on RSSI when needed.
      // FIXME TD-er: Must check WiFiEventData.wifi_connect_attempt to increase TX power
#if FEATURE_SET_WIFI_TX_PWR
      if (Settings.UseMaxTXpowerForSending()) {
        tx_pwr = Settings.getWiFi_TX_power();
      }
      SetWiFiTXpower(tx_pwr, candidate.rssi);
#endif
      // Start connect attempt now, so no longer needed to attempt new connection.
      WiFiEventData.wifiConnectAttemptNeeded = false;
      WiFiEventData.wifiConnectInProgress = true;
      const String key = WiFi_AP_CandidatesList::get_key(candidate.index);

#if FEATURE_USE_IPV6
      WiFi.enableIPv6(true);
#endif

      if ((Settings.HiddenSSID_SlowConnectPerBSSID() || !candidate.isHidden)
           && candidate.allowQuickConnect()) {
        WiFi.begin(candidate.ssid.c_str(), key.c_str(), candidate.channel, candidate.bssid.mac);
      } else {
        WiFi.begin(candidate.ssid.c_str(), key.c_str());
      }
      if (Settings.WaitWiFiConnect() || candidate.isHidden) {
//        WiFi.waitForConnectResult(candidate.isHidden ? 3000 : 1000);  // https://github.com/arendst/Tasmota/issues/14985
        WiFi.waitForConnectResult(1000);  // https://github.com/arendst/Tasmota/issues/14985
      }
      delay(1);
    } else {
      WiFiEventData.wifiConnectInProgress = false;
    }
  } else {
    if (!wifiAPmodeActivelyUsed() || WiFiEventData.wifiSetupConnect) {
      if (!prepareWiFi()) {
        //return;
      }

      if (WiFiScanAllowed()) {
        // Maybe not scan async to give the ESP some slack in power consumption?
        const bool async = false;
        WifiScan(async);
      }
      // Limit nr of attempts as we don't have any AP candidates.
      WiFiEventData.last_wifi_connect_attempt_moment.setMillisFromNow(60000);
      WiFiEventData.timerAPstart.setNow();
    }
  }

  logConnectionStatus();
}

// ********************************************************************************
// Set Wifi config
// ********************************************************************************
bool prepareWiFi() {
  #if defined(ESP32)
  registerWiFiEventHandler();
  #endif

  if (!WiFi_AP_Candidates.hasCandidateCredentials()) {
    if (!WiFiEventData.warnedNoValidWiFiSettings) {
      addLog(LOG_LEVEL_ERROR, F("WIFI : No valid wifi settings"));
      WiFiEventData.warnedNoValidWiFiSettings = true;
    }
//    WiFiEventData.last_wifi_connect_attempt_moment.clear();
    WiFiEventData.wifi_connect_attempt     = 1;
    WiFiEventData.wifiConnectAttemptNeeded = false;

    // No need to wait longer to start AP mode.
    if (!Settings.DoNotStartAP()) {
      WifiScan(false);
//      setAP(true);
    }
    return false;
  }
  WiFiEventData.warnedNoValidWiFiSettings = false;
  setSTA(true);

  #if defined(ESP8266)
  wifi_station_set_hostname(NetworkCreateRFCCompliantHostname().c_str());

  #endif // if defined(ESP8266)
  #if defined(ESP32)
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  #endif // if defined(ESP32)
  setConnectionSpeed();
  setupStaticIPconfig();
  WiFiEventData.wifiConnectAttemptNeeded = true;

  return true;
}

bool checkAndResetWiFi() {
  #ifdef ESP8266
  station_status_t status = wifi_station_get_connect_status();

  switch(status) {
    case STATION_GOT_IP:
      if (WiFi.RSSI() < 0 && WiFi.localIP().isSet()) {
        //if (WiFi.channel() == WiFiEventData.usedChannel || WiFiEventData.usedChannel == 0) {
          // This is a valid status, no need to reset
          return false;
        //}
      }
      break;
    case STATION_NO_AP_FOUND:
    case STATION_CONNECT_FAIL:
    case STATION_WRONG_PASSWORD:
      // Reason to reset WiFi
      break;
    case STATION_IDLE:
    case STATION_CONNECTING:
      if (WiFiEventData.last_wifi_connect_attempt_moment.isSet() && !WiFiEventData.last_wifi_connect_attempt_moment.timeoutReached(DEFAULT_WIFI_CONNECTION_TIMEOUT)) {
        return false;
      }
      break;
  }
  #endif
  #ifdef ESP32
  if (WiFi.isConnected()) {
    //if (WiFi.channel() == WiFiEventData.usedChannel || WiFiEventData.usedChannel == 0) {
      return false;
    //}
  }
  if (WiFiEventData.last_wifi_connect_attempt_moment.isSet() && !WiFiEventData.last_wifi_connect_attempt_moment.timeoutReached(DEFAULT_WIFI_CONNECTION_TIMEOUT)) {
    return false;
  }
  #endif
  # ifndef BUILD_NO_DEBUG
  String log = F("WiFi : WiFiConnected() out of sync: ");
  log += WiFiEventData.ESPeasyWifiStatusToString();
  log += F(" RSSI: ");
  log += String(WiFi.RSSI());
  #ifdef ESP8266
  log += F(" status: ");
  log += SDKwifiStatusToString(status);
  #endif
  #endif

  // Call for reset first, to make sure a syslog call will not try to send.
  resetWiFi();
  # ifndef BUILD_NO_DEBUG
  addLogMove(LOG_LEVEL_INFO, log);
  #endif
  return true;
}


void resetWiFi() {
  //if (wifiAPmodeActivelyUsed()) return;
  if (WiFiEventData.lastWiFiResetMoment.isSet() && !WiFiEventData.lastWiFiResetMoment.timeoutReached(1000)) {
    // Don't reset WiFi too often
    return;
  }
  FeedSW_watchdog();
  WiFiEventData.clearAll();
  WifiDisconnect();

  // Send this log only after WifiDisconnect() or else sending to syslog may cause issues
  addLog(LOG_LEVEL_INFO, F("Reset WiFi."));

  //  setWifiMode(WIFI_OFF);

  initWiFi();
}

#ifdef ESP32
void removeWiFiEventHandler()
{
  WiFi.removeEvent(WiFiEventData.wm_event_id);
  WiFiEventData.wm_event_id = 0;
}

void registerWiFiEventHandler()
{
  if (WiFiEventData.wm_event_id != 0) {
    removeWiFiEventHandler();
  }
  WiFiEventData.wm_event_id = WiFi.onEvent(WiFiEvent);
}
#endif


void initWiFi()
{
#ifdef ESP8266

  // See https://github.com/esp8266/Arduino/issues/5527#issuecomment-460537616
  // FIXME TD-er: Do not destruct WiFi object, it may cause crashes with queued UDP traffic.
//  WiFi.~ESP8266WiFiClass();
//  WiFi = ESP8266WiFiClass();
#endif // ifdef ESP8266
#ifdef ESP32
  removeWiFiEventHandler();
#endif


  WiFi.persistent(false); // Do not use SDK storage of SSID/WPA parameters
  // The WiFi.disconnect() ensures that the WiFi is working correctly. If this is not done before receiving WiFi connections,
  // those WiFi connections will take a long time to make or sometimes will not work at all.
  WiFi.disconnect(false);
  delay(1);
  if (active_network_medium != NetworkMedium_t::NotSet) {
    setSTA(true);
    WifiScan(false);
  }
  setWifiMode(WIFI_OFF);

#if defined(ESP32)
  registerWiFiEventHandler();
#endif
#ifdef ESP8266
  // WiFi event handlers
  static bool handlers_initialized = false;
  if (!handlers_initialized) {
    stationConnectedHandler = WiFi.onStationModeConnected(onConnected);
    stationDisconnectedHandler = WiFi.onStationModeDisconnected(onDisconnect);
    stationGotIpHandler = WiFi.onStationModeGotIP(onGotIP);
    stationModeDHCPTimeoutHandler = WiFi.onStationModeDHCPTimeout(onDHCPTimeout);
    stationModeAuthModeChangeHandler = WiFi.onStationModeAuthModeChanged(onStationModeAuthModeChanged);
    APModeStationConnectedHandler = WiFi.onSoftAPModeStationConnected(onConnectedAPmode);
    APModeStationDisconnectedHandler = WiFi.onSoftAPModeStationDisconnected(onDisconnectedAPmode);
    handlers_initialized = true;
  }
#endif
  delay(100);
}

// ********************************************************************************
// Configure WiFi TX power
// ********************************************************************************
#if FEATURE_SET_WIFI_TX_PWR
void SetWiFiTXpower() {
  SetWiFiTXpower(0); // Just some minimal value, will be adjusted in SetWiFiTXpower
}

void SetWiFiTXpower(float dBm) { 
  SetWiFiTXpower(dBm, WiFi.RSSI());
}

void SetWiFiTXpower(float dBm, float rssi) {
  const WiFiMode_t cur_mode = WiFi.getMode();
  if (cur_mode == WIFI_OFF) {
    return;
  }

  if (Settings.UseMaxTXpowerForSending()) {
    dBm = 30; // Just some max, will be limited later
  }

  // Range ESP32  : -1dBm - 20dBm
  // Range ESP8266: 0dBm - 20.5dBm
  float maxTXpwr;
  float threshold = GetRSSIthreshold(maxTXpwr);
  #ifdef ESP8266
  float minTXpwr{};
  #endif
  #ifdef ESP32
  float minTXpwr = -1.0f;
  #endif

  threshold += Settings.WiFi_sensitivity_margin; // Margin in dBm on top of threshold

  // Assume AP sends with max set by ETSI standard.
  // 2.4 GHz: 100 mWatt (20 dBm)
  // US and some other countries allow 1000 mW (30 dBm)
  // We cannot send with over 20 dBm, thus it makes no sense to force higher TX power all the time.
  const float newrssi = rssi - 20;
  if (newrssi < threshold) {
    minTXpwr = threshold - newrssi;
  }
  if (minTXpwr > maxTXpwr) {
    minTXpwr = maxTXpwr;
  }
  if (dBm > maxTXpwr) {
    dBm = maxTXpwr;
  } else if (dBm < minTXpwr) {
    dBm = minTXpwr;
  }

  #ifdef ESP32
  int8_t power = dBm * 4;
  if (esp_wifi_set_max_tx_power(power) == ESP_OK)  {
    if (esp_wifi_get_max_tx_power(&power) == ESP_OK)  {
      dBm = static_cast<float>(power) / 4.0f;
    }
  }
  #endif

  #ifdef ESP8266
  WiFi.setOutputPower(dBm);
  #endif

  if (WiFiEventData.wifi_TX_pwr < dBm) {
    // Will increase the TX power, give power supply of the unit some rest
    delay(1);
  }

  WiFiEventData.wifi_TX_pwr = dBm;

  delay(0);
  #ifndef BUILD_NO_DEBUG
  if (loglevelActiveFor(LOG_LEVEL_DEBUG)) {
    const int TX_pwr_int = WiFiEventData.wifi_TX_pwr * 4;
    const int maxTXpwr_int = maxTXpwr * 4;
    if (TX_pwr_int != maxTXpwr_int) {
      static int last_log = -1;
      if (TX_pwr_int != last_log) {
        last_log = TX_pwr_int;
        String log = strformat(
          F("WiFi : Set TX power to %ddBm sensitivity: %ddBm"),
          static_cast<int>(dBm),
          static_cast<int>(threshold));
        if (rssi < 0) {
          log += strformat(F(" RSSI: %ddBm"), static_cast<int>(rssi));
        }
        addLogMove(LOG_LEVEL_DEBUG, log);
      }
    }
  }
  #endif
}
#endif




float GetRSSIthreshold(float& maxTXpwr) {
  maxTXpwr = Settings.getWiFi_TX_power();
  float threshold = WIFI_SENSITIVITY_n;
  switch (getConnectionProtocol()) {
    case WiFiConnectionProtocol::WiFi_Protocol_11b:
      threshold = WIFI_SENSITIVITY_11b;
      if (maxTXpwr > MAX_TX_PWR_DBM_11b) maxTXpwr = MAX_TX_PWR_DBM_11b;
      break;
    case WiFiConnectionProtocol::WiFi_Protocol_11g:
      threshold = WIFI_SENSITIVITY_54g;
      if (maxTXpwr > MAX_TX_PWR_DBM_54g) maxTXpwr = MAX_TX_PWR_DBM_54g;
      break;
#ifdef ESP8266
    case WiFiConnectionProtocol::WiFi_Protocol_11n:
#else
    case WiFiConnectionProtocol::WiFi_Protocol_HT20:
    case WiFiConnectionProtocol::WiFi_Protocol_HT40:
    case WiFiConnectionProtocol::WiFi_Protocol_HE20:
#endif

      threshold = WIFI_SENSITIVITY_n;
      if (maxTXpwr > MAX_TX_PWR_DBM_n) maxTXpwr = MAX_TX_PWR_DBM_n;
      break;
#ifdef ESP32
    case WiFiConnectionProtocol::WiFi_Protocol_LR:
#endif
    case WiFiConnectionProtocol::Unknown:
      break;
  }
  return threshold;
}

int GetRSSI_quality() {
  long rssi = WiFi.RSSI();

  if (-50 < rssi) { return 10; }

  if (rssi <= -98) { return 0;  }
  rssi = rssi + 97; // Range 0..47 => 1..9
  return (rssi / 5) + 1;
}

WiFiConnectionProtocol getConnectionProtocol() {
  if (WiFi.RSSI() < 0) {
    #ifdef ESP8266
    switch (wifi_get_phy_mode()) {
      case PHY_MODE_11B:
        return WiFiConnectionProtocol::WiFi_Protocol_11b;
      case PHY_MODE_11G:
        return WiFiConnectionProtocol::WiFi_Protocol_11g;
      case PHY_MODE_11N:
        return WiFiConnectionProtocol::WiFi_Protocol_11n;
    }
    #endif
    #ifdef ESP32

    wifi_phy_mode_t phymode;
    esp_wifi_sta_get_negotiated_phymode(&phymode);
    switch (phymode) {
      case WIFI_PHY_MODE_11B: return WiFiConnectionProtocol::WiFi_Protocol_11b;
      case WIFI_PHY_MODE_11G: return WiFiConnectionProtocol::WiFi_Protocol_11g;
      case WIFI_PHY_MODE_HT20: return WiFiConnectionProtocol::WiFi_Protocol_HT20;
      case WIFI_PHY_MODE_HT40: return WiFiConnectionProtocol::WiFi_Protocol_HT40;
      case WIFI_PHY_MODE_HE20: return WiFiConnectionProtocol::WiFi_Protocol_HE20;
      case WIFI_PHY_MODE_LR: return WiFiConnectionProtocol::WiFi_Protocol_LR;
    }
    #endif
  }
  return WiFiConnectionProtocol::Unknown;
}

#ifdef ESP32
int64_t WiFi_get_TSF_time()
{
  return esp_wifi_get_tsf_time(WIFI_IF_STA);
}
#endif


// ********************************************************************************
// Disconnect from Wifi AP
// ********************************************************************************
void WifiDisconnect()
{
  if (!WiFiEventData.processedDisconnect || 
       WiFiEventData.processingDisconnect.isSet()) {
    return;
  }
  // Prevent recursion
  static LongTermTimer processingDisconnectTimer;
  if (processingDisconnectTimer.isSet() && 
     !processingDisconnectTimer.timeoutReached(200)) return;
  processingDisconnectTimer.setNow();
  # ifndef BUILD_NO_DEBUG
  addLog(LOG_LEVEL_INFO, F("WiFi : WifiDisconnect()"));
  #endif
  #ifdef ESP32
  removeWiFiEventHandler();
  WiFi.disconnect();
  delay(100);
  {
    const IPAddress ip;
    const IPAddress gw;
    const IPAddress subnet;
    const IPAddress dns;
    WiFi.config(ip, gw, subnet, dns);
  }
  #endif
  #ifdef ESP8266
  // Only call disconnect when STA is active
  if (WifiIsSTA(WiFi.getMode())) {
    wifi_station_disconnect();
  }
  station_config conf{};
  memset(&conf, 0, sizeof(conf));
  ETS_UART_INTR_DISABLE();
  wifi_station_set_config_current(&conf);
  ETS_UART_INTR_ENABLE();
  #endif
  WiFiEventData.setWiFiDisconnected();
  WiFiEventData.markDisconnect(WIFI_DISCONNECT_REASON_UNSPECIFIED);
  /*
  if (!Settings.UseLastWiFiFromRTC()) {
    RTC.clearLastWiFi();
  }
  */
  delay(100);
  WiFiEventData.processingDisconnect.clear();
  WiFiEventData.processedDisconnect = false;
  processDisconnect();
  processingDisconnectTimer.clear();
}

// ********************************************************************************
// Scan WiFi network
// ********************************************************************************
bool WiFiScanAllowed() {
  if (WiFi_AP_Candidates.scanComplete() == WIFI_SCAN_RUNNING) {
    return false;
  }
  if (!WiFiEventData.processedScanDone) { 
    processScanDone(); 
  }
  if (!WiFiEventData.processedDisconnect) {
    processDisconnect();
  }

  if (WiFiEventData.wifiConnectInProgress) {
    return false;
  }

  if (WiFiEventData.intent_to_reboot) {
    return false;
  }

  if (WiFiEventData.unprocessedWifiEvents()) {
    # ifndef BUILD_NO_DEBUG
    if (loglevelActiveFor(LOG_LEVEL_ERROR)) {
      String log = F("WiFi : Scan not allowed, unprocessed WiFi events: ");
      if (!WiFiEventData.processedConnect) {
        log += F(" conn");
      }
      if (!WiFiEventData.processedDisconnect) {
        log += F(" disconn");
      }
      if (!WiFiEventData.processedGotIP) {
        log += F(" gotIP");
      }
      if (!WiFiEventData.processedDHCPTimeout) {
        log += F(" DHCP_t/o");
      }
      
      addLogMove(LOG_LEVEL_ERROR, log);
      logConnectionStatus();
    }
    #endif
    return false;
  }
  /*
  if (!wifiAPmodeActivelyUsed() && !NetworkConnected()) {
    return true;
  }
  */
  WiFi_AP_Candidates.purge_expired();
  if (WiFiEventData.wifiConnectInProgress) {
    return false;
  }
  if (WiFiEventData.lastScanMoment.isSet()) {
    if (NetworkConnected() && WiFi_AP_Candidates.getBestCandidate().usable()) {
      # ifndef BUILD_NO_DEBUG
      addLog(LOG_LEVEL_ERROR, F("WiFi : Scan not needed, good candidate present"));
      #endif
      return false;
    }
  }

  if (WiFiEventData.lastDisconnectMoment.isSet() && WiFiEventData.lastDisconnectMoment.millisPassedSince() < WIFI_RECONNECT_WAIT) {
    if (!NetworkConnected()) {
      return WiFiEventData.processedConnect;
    }
  }
  if (WiFiEventData.lastScanMoment.isSet()) {
    const LongTermTimer::Duration scanInterval = wifiAPmodeActivelyUsed() ? WIFI_SCAN_INTERVAL_AP_USED : WIFI_SCAN_INTERVAL_MINIMAL;
    if (WiFiEventData.lastScanMoment.millisPassedSince() < scanInterval) {
      return false;
    }
  }
  return WiFiEventData.processedConnect;
}


void WifiScan(bool async, uint8_t channel) {
  setSTA(true);
  if (!WiFiScanAllowed()) {
    return;
  }
#ifdef ESP32
  // TD-er: Don't run async scan on ESP32.
  // Since IDF 4.4 it seems like the active channel may be messed up when running async scan
  // Perform a disconnect after scanning.
  // See: https://github.com/letscontrolit/ESPEasy/pull/3579#issuecomment-967021347
  async = false;
#endif

  START_TIMER;
  WiFiEventData.lastScanMoment.setNow();
  # ifndef BUILD_NO_DEBUG
  if (loglevelActiveFor(LOG_LEVEL_INFO)) {
    if (channel == 0) {
      addLog(LOG_LEVEL_INFO, F("WiFi : Start network scan all channels"));
    } else {
      addLogMove(LOG_LEVEL_INFO, strformat(F("WiFi : Start network scan ch: %d "), channel));
    }
  }
  #endif
  bool show_hidden         = true;
  WiFiEventData.processedScanDone = false;
  WiFiEventData.lastGetScanMoment.setNow();
  WiFiEventData.lastScanChannel = channel;

  unsigned int nrScans = 1 + (async ? 0 : Settings.NumberExtraWiFiScans);
  while (nrScans > 0) {
    if (!async) {
      WiFi_AP_Candidates.begin_sync_scan();
      FeedSW_watchdog();
    }
    --nrScans;
#ifdef ESP8266
#if FEATURE_ESP8266_DIRECT_WIFI_SCAN
    {
      static bool FIRST_SCAN = true;

      struct scan_config config;
      memset(&config, 0, sizeof(config));
      config.ssid = nullptr;
      config.bssid = nullptr;
      config.channel = channel;
      config.show_hidden = show_hidden ? 1 : 0;;
      config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
      if (FIRST_SCAN) {
        config.scan_time.active.min = 100;
        config.scan_time.active.max = 200;
      } else {
        config.scan_time.active.min = 400;
        config.scan_time.active.max = 500;
      }
      FIRST_SCAN = false;
      wifi_station_scan(&config, &onWiFiScanDone);
      if (!async) {
        // will resume when SYSTEM_EVENT_SCAN_DONE event is fired
        do {
          delay(0);
        } while (!WiFiEventData.processedScanDone);
      }
 
    }
#else
    WiFi.scanNetworks(async, show_hidden, channel);
#endif
#endif
#ifdef ESP32
    const bool passive = false;
    const uint32_t max_ms_per_chan = 300;
    WiFi.scanNetworks(async, show_hidden, passive, max_ms_per_chan /*, channel */);
#endif
    if (!async) {
      FeedSW_watchdog();
      processScanDone();
    }
  }
#if FEATURE_TIMING_STATS
  if (async) {
    STOP_TIMER(WIFI_SCAN_ASYNC);
  } else {
    STOP_TIMER(WIFI_SCAN_SYNC);
  }
#endif

#ifdef ESP32
#if ESP_IDF_VERSION_MAJOR<5
  RTC.clearLastWiFi();
  if (WiFiConnected()) {
    # ifndef BUILD_NO_DEBUG
    addLog(LOG_LEVEL_INFO, F("WiFi : Disconnect after scan"));
    #endif

    const bool needReconnect = WiFiEventData.wifiConnectAttemptNeeded;
    WifiDisconnect();
    WiFiEventData.wifiConnectAttemptNeeded = needReconnect;
  }
#endif
#endif

}

// ********************************************************************************
// Scan all Wifi Access Points
// ********************************************************************************
void WiFiScan_log_to_serial()
{
  // Direct Serial is allowed here, since this function will only be called from serial input.
  serialPrintln(F("WIFI : SSID Scan start"));
  if (WiFi_AP_Candidates.scanComplete() <= 0) {
    WiFiMode_t cur_wifimode = WiFi.getMode();
    WifiScan(false);
    setWifiMode(cur_wifimode);
  }

  const int8_t scanCompleteStatus = WiFi_AP_Candidates.scanComplete();
  if (scanCompleteStatus <= 0) {
    serialPrintln(F("WIFI : No networks found"));
  }
  else
  {
    serialPrint(F("WIFI : "));
    serialPrint(String(scanCompleteStatus));
    serialPrintln(F(" networks found"));

    int i = 0;

    for (auto it = WiFi_AP_Candidates.scanned_begin(); it != WiFi_AP_Candidates.scanned_end(); ++it)
    {
      ++i;
      // Print SSID and RSSI for each network found
      serialPrint(F("WIFI : "));
      serialPrint(String(i));
      serialPrint(": ");
      serialPrintln(it->toString());
      delay(10);
    }
  }
  serialPrintln("");
}

// ********************************************************************************
// Manage Wifi Modes
// ********************************************************************************
void setSTA(bool enable) {
  switch (WiFi.getMode()) {
    case WIFI_OFF:

      if (enable) { setWifiMode(WIFI_STA); }
      break;
    case WIFI_STA:

      if (!enable) { setWifiMode(WIFI_OFF); }
      break;
    case WIFI_AP:

      if (enable) { setWifiMode(WIFI_AP_STA); }
      break;
    case WIFI_AP_STA:

      if (!enable) { setWifiMode(WIFI_AP); }
      break;
    default:
      break;
  }
}

void setAP(bool enable) {
  WiFiMode_t wifimode = WiFi.getMode();

  switch (wifimode) {
    case WIFI_OFF:

      if (enable) { 
        setWifiMode(WIFI_AP); 
      }
      break;
    case WIFI_STA:

      if (enable) { setWifiMode(WIFI_AP_STA); }
      break;
    case WIFI_AP:

      if (!enable) { setWifiMode(WIFI_OFF); }
      break;
    case WIFI_AP_STA:

      if (!enable) { setWifiMode(WIFI_STA); }
      break;
    default:
      break;
  }
}

// Only internal scope
void setAPinternal(bool enable)
{
  if (enable) {
    // create and store unique AP SSID/PW to prevent ESP from starting AP mode with default SSID and No password!
    // setup ssid for AP Mode when needed
    String softAPSSID = NetworkCreateRFCCompliantHostname();
    String pwd        = SecuritySettings.WifiAPKey;
    IPAddress subnet(DEFAULT_AP_SUBNET);

    if (!WiFi.softAPConfig(apIP, apIP, subnet)) {
      addLog(LOG_LEVEL_ERROR, strformat(
        ("WIFI : [AP] softAPConfig failed! IP: %s, GW: %s, SN: %s"),
        apIP.toString().c_str(), 
        apIP.toString().c_str(), 
        subnet.toString().c_str()
      )
      );
    }

    int channel = 1;
    if (WifiIsSTA(WiFi.getMode()) && WiFiConnected()) {
      channel = WiFi.channel();
    }

    if (WiFi.softAP(softAPSSID.c_str(), pwd.c_str(), channel)) {
      eventQueue.add(F("WiFi#APmodeEnabled"));
      if (loglevelActiveFor(LOG_LEVEL_INFO)) {
        addLogMove(LOG_LEVEL_INFO, strformat(
          F("WIFI : AP Mode enabled. SSID: %s IP: %s ch: %d"),
          softAPSSID.c_str(),
          formatIP(WiFi.softAPIP()).c_str(),
          channel));
      }
    } else {
      if (loglevelActiveFor(LOG_LEVEL_ERROR)) {
        addLogMove(LOG_LEVEL_ERROR, strformat(
          F("WIFI : Error while starting AP Mode with SSID: %s IP: %s"),
          softAPSSID.c_str(),
          formatIP(apIP).c_str()));
      }
    }
    #ifdef ESP32

    #else // ifdef ESP32

    if (wifi_softap_dhcps_status() != DHCP_STARTED) {
      if (!wifi_softap_dhcps_start()) {
        addLog(LOG_LEVEL_ERROR, F("WIFI : [AP] wifi_softap_dhcps_start failed!"));
      }
    }
    #endif // ifdef ESP32
    WiFiEventData.timerAPoff.setMillisFromNow(WIFI_AP_OFF_TIMER_DURATION);
  } else {
    #if FEATURE_DNS_SERVER
    if (dnsServerActive) {
      dnsServerActive = false;
      dnsServer.stop();
    }
    #endif // if FEATURE_DNS_SERVER
  }
}

const __FlashStringHelper * getWifiModeString(WiFiMode_t wifimode)
{
  switch (wifimode) {
    case WIFI_OFF:   return F("OFF");
    case WIFI_STA:   return F("STA");
    case WIFI_AP:    return F("AP");
    case WIFI_AP_STA: return F("AP+STA");
    default:
      break;
  }
  return F("Unknown");
}

void setWifiMode(WiFiMode_t new_mode) {
  const WiFiMode_t cur_mode = WiFi.getMode();
  static WiFiMode_t processing_wifi_mode = cur_mode;
  if (cur_mode == new_mode) {
    return;
  }
  if (processing_wifi_mode == new_mode) {
    // Prevent loops
    return;
  }
  processing_wifi_mode = new_mode;

  if (cur_mode == WIFI_OFF) {
    #if defined(ESP32)
    // Needs to be set while WiFi is off
    WiFi.hostname(NetworkCreateRFCCompliantHostname());
    #endif
    WiFiEventData.markWiFiTurnOn();
  }
  if (new_mode != WIFI_OFF) {
    #ifdef ESP8266
    // See: https://github.com/esp8266/Arduino/issues/6172#issuecomment-500457407
    WiFi.forceSleepWake(); // Make sure WiFi is really active.
    #endif
    delay(100);
  } else {
    WifiDisconnect();
//    delay(100);
    processDisconnect();
    WiFiEventData.clear_processed_flags();
  }

  addLog(LOG_LEVEL_INFO, concat(F("WIFI : Set WiFi to "), getWifiModeString(new_mode)));

  int retry = 2;
  while (!WiFi.mode(new_mode) && retry > 0) {
    addLog(LOG_LEVEL_INFO, F("WIFI : Cannot set mode!!!!!"));
    delay(100);
    --retry;
  }
  retry = 2;
  while (WiFi.getMode() != new_mode && retry > 0) {
    addLog(LOG_LEVEL_INFO, F("WIFI : mode not yet set"));
    delay(100);
    --retry;
  }


  if (new_mode == WIFI_OFF) {
    WiFiEventData.markWiFiTurnOn();
    #if defined(ESP32)
    // Needs to be set while WiFi is off
    WiFi.hostname(NetworkCreateRFCCompliantHostname());
    #endif
    delay(100);
    #if defined(ESP32)
    esp_wifi_set_ps(WIFI_PS_NONE); 
//    esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
    #endif
    #ifdef ESP8266
    WiFi.forceSleepBegin();
    #endif // ifdef ESP8266
    delay(1);
  } else {
    #ifdef ESP32
    if (cur_mode == WIFI_OFF) {
      registerWiFiEventHandler();
    }
    #endif
    // Only set power mode when AP is not enabled
    // When AP is enabled, the sleep mode is already set to WIFI_NONE_SLEEP
    if (!WifiIsAP(new_mode)) {
      if (Settings.WifiNoneSleep()) {
        #ifdef ESP8266
        WiFi.setSleepMode(WIFI_NONE_SLEEP);
        #endif
        #ifdef ESP32
        WiFi.setSleep(WIFI_PS_NONE);
        #endif
      } else if (Settings.EcoPowerMode()) {
        // Allow light sleep during idle times
        #ifdef ESP8266
        WiFi.setSleepMode(WIFI_LIGHT_SLEEP);
        #endif
        #ifdef ESP32
        // Maximum modem power saving. 
        // In this mode, interval to receive beacons is determined by the listen_interval parameter in wifi_sta_config_t
        // FIXME TD-er: Must test if this is desired behavior in ESP32.
        WiFi.setSleep(WIFI_PS_MAX_MODEM);
        #endif
      } else {
        // Default
        #ifdef ESP8266
        WiFi.setSleepMode(WIFI_MODEM_SLEEP);
        #endif
        #ifdef ESP32
        // Minimum modem power saving. 
        // In this mode, station wakes up to receive beacon every DTIM period
        WiFi.setSleep(WIFI_PS_MIN_MODEM);
        #endif
      }
    }
#if FEATURE_SET_WIFI_TX_PWR
    SetWiFiTXpower();
#endif
    if (WifiIsSTA(new_mode)) {
      WiFi.setAutoConnect(Settings.SDK_WiFi_autoreconnect());
      WiFi.setAutoReconnect(Settings.SDK_WiFi_autoreconnect());
    }
    delay(100); // Must allow for some time to init.
  }
  const bool new_mode_AP_enabled = WifiIsAP(new_mode);

  if (WifiIsAP(cur_mode) && !new_mode_AP_enabled) {
    eventQueue.add(F("WiFi#APmodeDisabled"));
  }

  if (WifiIsAP(cur_mode) != new_mode_AP_enabled) {
    // Mode has changed
    setAPinternal(new_mode_AP_enabled);
  }
  #if FEATURE_MDNS
  #ifdef ESP8266
  // notifyAPChange() is not present in the ESP32 MDNSResponder
  MDNS.notifyAPChange();
  #endif
  #endif
}

bool WifiIsAP(WiFiMode_t wifimode)
{
  #if defined(ESP32)
  return (wifimode == WIFI_MODE_AP) || (wifimode == WIFI_MODE_APSTA);
  #else // if defined(ESP32)
  return (wifimode == WIFI_AP) || (wifimode == WIFI_AP_STA);
  #endif // if defined(ESP32)
}

bool WifiIsSTA(WiFiMode_t wifimode)
{
  #if defined(ESP32)
  return (wifimode & WIFI_MODE_STA) != 0;
  #else // if defined(ESP32)
  return (wifimode & WIFI_STA) != 0;
  #endif // if defined(ESP32)
}

bool WiFiUseStaticIP() {
  return Settings.IP[0] != 0 && Settings.IP[0] != 255;
}

bool wifiAPmodeActivelyUsed()
{
  if (!WifiIsAP(WiFi.getMode()) || (!WiFiEventData.timerAPoff.isSet())) {
    // AP not active or soon to be disabled in processDisableAPmode()
    return false;
  }
  return WiFi.softAPgetStationNum() != 0;

  // FIXME TD-er: is effectively checking for AP active enough or must really check for connected clients to prevent automatic wifi
  // reconnect?
}

void setConnectionSpeed() {
  #ifdef ESP8266
  // ESP8266 only supports 802.11g mode when running in STA+AP
  const bool forcedByAPmode = WifiIsAP(WiFi.getMode());
  WiFiPhyMode_t phyMode = (Settings.ForceWiFi_bg_mode() || forcedByAPmode) ? WIFI_PHY_MODE_11G : WIFI_PHY_MODE_11N;
  if (!forcedByAPmode) {
    const WiFi_AP_Candidate candidate = WiFi_AP_Candidates.getCurrent();
    if (candidate.phy_known() && (candidate.phy_11g != candidate.phy_11n)) {
      if ((WIFI_PHY_MODE_11G == phyMode) && !candidate.phy_11g) {
        phyMode = WIFI_PHY_MODE_11N;
        addLog(LOG_LEVEL_INFO, F("WIFI : AP is set to 802.11n only"));
      } else if ((WIFI_PHY_MODE_11N == phyMode) && !candidate.phy_11n) {
        phyMode = WIFI_PHY_MODE_11G;
        addLog(LOG_LEVEL_INFO, F("WIFI : AP is set to 802.11g only"));
      }      
    } else {
      bool useAlternate = WiFiEventData.connectionFailures > 10;
      if (useAlternate) {
        phyMode = (WIFI_PHY_MODE_11G == phyMode) ? WIFI_PHY_MODE_11N : WIFI_PHY_MODE_11G;
      }
    }
  } else {
    // No need to perform a next attempt.
    WiFi_AP_Candidates.markAttempt();
  }

  if (WiFi.getPhyMode() == phyMode) {
    return;
  }
  #ifndef BUILD_NO_DEBUG
  if (loglevelActiveFor(LOG_LEVEL_INFO)) {
    String log = concat(F("WIFI : Set to 802.11"), (WIFI_PHY_MODE_11G == phyMode) ? 'g' : 'n');
    if (forcedByAPmode) {
      log += (F(" (AP+STA mode)"));
    }
    if (Settings.ForceWiFi_bg_mode()) {
      log += F(" Force B/G mode");
    }
    addLogMove(LOG_LEVEL_INFO, log);
  }
  #endif
  WiFi.setPhyMode(phyMode);
  #endif // ifdef ESP8266

  // Does not (yet) work, so commented out.
  #ifdef ESP32

  // HT20 = 20 MHz channel width.
  // HT40 = 40 MHz channel width.
  // In theory, HT40 can offer upto 150 Mbps connection speed.
  // However since HT40 is using nearly all channels on 2.4 GHz WiFi,
  // Thus you are more likely to experience disturbances.
  // The response speed and stability is better at HT20 for ESP units.
  esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);

  uint8_t protocol = WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G; // Default to BG

  if (!Settings.ForceWiFi_bg_mode() || (WiFiEventData.connectionFailures > 10)) {
    // Set to use BGN
    protocol |= WIFI_PROTOCOL_11N;
    #ifdef ESP32C6
    protocol |= WIFI_PROTOCOL_11AX;
    #endif
  }

  const WiFi_AP_Candidate candidate = WiFi_AP_Candidates.getCurrent();
  if (candidate.phy_known()) {
    // Check to see if the access point is set to "N-only"
    if ((protocol & WIFI_PROTOCOL_11N) == 0) {
      if (!candidate.phy_11b && !candidate.phy_11g && candidate.phy_11n) {
        if (candidate.phy_11n) {
          // Set to use BGN
          protocol |= WIFI_PROTOCOL_11N;
          addLog(LOG_LEVEL_INFO, F("WIFI : AP is set to 802.11n only"));
        }
#ifdef ESP32C6
        if (candidate.phy_11ax) {
          // Set to use WiFi6
          protocol |= WIFI_PROTOCOL_11AX;
          addLog(LOG_LEVEL_INFO, F("WIFI : AP is set to 802.11ax"));
        }
#endif
      }
    }
  }


  if (WifiIsSTA(WiFi.getMode())) {
    // Set to use "Long GI" making it more resilliant to reflections
    // See: https://www.tp-link.com/us/configuration-guides/q_a_basic_wireless_concepts/?configurationId=2958#_idTextAnchor038
    esp_wifi_config_80211_tx_rate(WIFI_IF_STA, WIFI_PHY_RATE_MCS3_LGI);
    esp_wifi_set_protocol(WIFI_IF_STA, protocol);
  }

  if (WifiIsAP(WiFi.getMode())) {
    esp_wifi_set_protocol(WIFI_IF_AP, protocol);
  }
  #endif // ifdef ESP32
}

void setupStaticIPconfig() {
  setUseStaticIP(WiFiUseStaticIP());

  if (!WiFiUseStaticIP()) { return; }
  const IPAddress ip     (Settings.IP);
  const IPAddress gw     (Settings.Gateway);
  const IPAddress subnet (Settings.Subnet);
  const IPAddress dns    (Settings.DNS);

  WiFiEventData.dns0_cache = dns;

  WiFi.config(ip, gw, subnet, dns);

  if (loglevelActiveFor(LOG_LEVEL_INFO)) {
    addLogMove(LOG_LEVEL_INFO, strformat(
      F("IP   : Static IP : %s GW: %s SN: %s DNS: %s"),
      formatIP(ip).c_str(),
      formatIP(gw).c_str(),
      formatIP(subnet).c_str(),
      getValue(LabelType::DNS).c_str()));
  }
}

// ********************************************************************************
// Formatting WiFi related strings
// ********************************************************************************
String formatScanResult(int i, const String& separator) {
  int32_t rssi = 0;

  return formatScanResult(i, separator, rssi);
}

String formatScanResult(int i, const String& separator, int32_t& rssi) {
  WiFi_AP_Candidate tmp(i);
  rssi = tmp.rssi;
  return tmp.toString(separator);
}


void logConnectionStatus() {
  static unsigned long lastLog = 0;
  if (lastLog != 0 && timePassedSince(lastLog) < 1000) {
    return;
  }
  lastLog = millis();
#ifndef BUILD_NO_DEBUG
  #ifdef ESP8266
  const uint8_t arduino_corelib_wifistatus = WiFi.status();
  const uint8_t sdk_wifistatus             = wifi_station_get_connect_status();

  if ((arduino_corelib_wifistatus == WL_CONNECTED) != (sdk_wifistatus == STATION_GOT_IP)) {
    if (loglevelActiveFor(LOG_LEVEL_ERROR)) {
      String log = F("WiFi : SDK station status differs from Arduino status. SDK-status: ");
      log += SDKwifiStatusToString(sdk_wifistatus);
      log += F(" Arduino status: ");
      log += ArduinoWifiStatusToString(arduino_corelib_wifistatus);
      addLogMove(LOG_LEVEL_ERROR, log);
    }
  }
  #endif

  if (loglevelActiveFor(LOG_LEVEL_INFO)) {
    addLogMove(LOG_LEVEL_INFO, strformat(
      F("WIFI : Arduino wifi status: %s ESPeasy internal wifi status: %s"),
      ArduinoWifiStatusToString(WiFi.status()).c_str(),
      WiFiEventData.ESPeasyWifiStatusToString().c_str()));
  }
/*
  if (loglevelActiveFor(LOG_LEVEL_INFO)) {
    String log;

    switch (WiFi.status()) {
      case WL_NO_SSID_AVAIL: {
        log = F("WIFI : No SSID found matching: ");
        break;
      }
      case WL_CONNECT_FAILED: {
        log = F("WIFI : Connection failed to: ");
        break;
      }
      case WL_DISCONNECTED: {
        log = F("WIFI : WiFi.status() = WL_DISCONNECTED  SSID: ");
        break;
      }
      case WL_IDLE_STATUS: {
        log = F("WIFI : Connection in IDLE state: ");
        break;
      }
      case WL_CONNECTED: {
        break;
      }
      default:
        break;
    }

    if (log.length() > 0) {
      const char *ssid = getLastWiFiSettingsSSID();
      log += ssid;
      addLog(LOG_LEVEL_INFO, log);
    }
  }
  */
#endif // ifndef BUILD_NO_DEBUG
}
