# Nintendo classic i2c controller user space driver for Raspberry Pi

Allows connecting NES/SNES classic mini controllers directly via the I2C bus on the GPIO header of the Raspberry Pi.
The driver also supports Wii Classic Controller (Pro).

## Connecting the controller
Plug side view:
```
/---------\
|         |
|  1-2-3  |
|  4-5-6  |
|  _____  |
\_/     \_/
```
| Controller Pin | Function      | Pi Pin    |
|---------------:|---------------|-----------|
| 1              | I2C data      | 3 (SDA)   |
| 2              | device detect | -         |
| 3              | +3.3V         | 1 (+3.3V) |
| 4              | GND           | 6 (GND)   |
| 5              | not connected | -         |
| 6              | I2C clock     | 5 (SCL)   |

One way to connect the wires without breaking the plug of your controller is to use one of many "nunchuck" breakout
boards sold by hobby electronic stores. Some of them seem to be out of stock by now so an alternative would be to
sacrifice a socket part from an snes/nes classic extension cable which are currently common and inexpensive.

## Enabling i2c on Raspberry Pi
This can be done with the bundled configuration tool `raspi-config` under "Interfacing Options" > "I2C".
If you want to do it manually, it comes down to:
* Adding `dtparam=i2c_arm=on` line to `/boot/config.txt`
* Loading `i2c_dev` module (`sudo modprobe i2c_dev` for the current session and adding `i2c_dev` line to `/etc/modules` to load on boot)

## Installing required packages

```
sudo apt-get install build-essential cmake libi2c-dev i2c-tools
```

## Testing the connection

When properly connected it controller should be now visible at address 0x52 and all other addresses should
be empty.
```
$ sudo i2cdetect -y 1
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:          -- -- -- -- -- -- -- -- -- -- -- -- -- 
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
50: -- -- 52 -- -- -- -- -- -- -- -- -- -- -- -- -- 
60: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
70: -- -- -- -- -- -- -- -- 
```

## Compiling the driver
```
cmake .
make
```
## Testing the driver

```
sudo ./i2c-classic-controller -d
```
If the controller is correctly connected it should display the button events:
```
Detected device: 01 00 A4 20 01 01
buttons:    A           
```
## Driver options
* `-y <n>` by default the driver is using `/dev/i2c-1` for the i2c bus connection, which corresponds to the bus
exposed on Pi pins 3/5. This option allows using other buses.
* `-f <n>` event polling frequency. The default is 60 per second and can be changed in range 1 - 1000.
* `-d` print controller events to the standard output
* `-a` report analog stick events. By default the driver will ignore the analog input which is not presnet on the classic
controllers. It seems that the classic console controllers reuse the Wii Classic Controller Pro hardware, so they cannot
be distinguished.
* `-a6` report also the analog triggers. The triggers also report separate digital events, so enabling this may confuse
some configuration software. Also in Wii Classic Controller Pro those buttons are not even analog so only 2 values are
reported. It is possible that the original Wii Classic Controller (which I don't own) uses true analog output for those.
* `-h` use full 8-bit accuracy readout for analog input. The snes/nes classic consoles use this mode even though the controllers don't have analog sticks. This option may be required for some third party controllers which only implement
the 8-bit accuracy readout.
## Installing

```
sudo make install
```
This will install a systemd service which will run the driver on startup. Additionally a udev rule will be set
to remove `ID_INPUT_KEY` property from the uinput node which prevents some software (specifically Kodi) to detect
the controller as a keyboard.

## Connecting more than one controller

All Nintendo i2c controllers use the same static i2c address which means only one can be connected to an i2c bus at a time.
Raspberry Pi 2/3 SoC has 2 buses but bus 0 is used for internal purposes. Pi 4 has more i2c busses however this
functionality does not seem to be explored much so I did not try to use it.
Either way this driver theoretically would support multiple controllers when one instance of the driver is run for
each port (using the `-y` bus id param).
