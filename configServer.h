#pragma once

extern bool wifiConnected;

void configServerStart(String ssid, String passwd);
void configServerLoop();
