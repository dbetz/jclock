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

The clock has five settings. You can adjust the settings by first pressing the
encoder knob. This puts the clock in parameter selection mode. You can select
the parameter to adjust by turning the knob.

Once you've selected the parameter you want to adjust, press the knob again.
This puts the clock in parameter adjustment mode. You can adjust the parameter
value by turning the knob. When you've selected the value you want for the
parameter press the knob again to save the new value.

The parameters are:

1 - the meditation timer period in minutes
2 - the Pomodoro work timer period in minutes
3 - the number of Pomodoro work periods
4 - the Pomodoro short rest period in minutes
5 - the Pomodoro long rest period in minutes