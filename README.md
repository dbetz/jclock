# jclock
Jonathan's Clock Project

Use the JClock web application to set the SSID and password as well as select
time timezone.

Connect pin 6 to GND and press the reset button.

You should then see a wifi access point named "JClock-xxx" where xxx is the unique ID
of the ESP32-S3 Feather module. The password is "jclock86".

Connect to the IP address 192.168.1.1 and you should see the configuration page.

Use the configuration page to set the SSID, password, and timezone.

Remove the jumper from pin 6 to GND and press reset. The clock should start with
the new settings.