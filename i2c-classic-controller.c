#include <linux/i2c-dev.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define UINPUT_DEV_NAME "Nintendo I²C Controller"

#define GET(r, i) ((r & (1 << i)) == 0)

int classic_button_map[16] = {
  0,
  BTN_TR,
  BTN_START,
  BTN_MODE,
  BTN_SELECT,
  BTN_TL,
  BTN_DPAD_DOWN,
  BTN_DPAD_RIGHT,
  BTN_DPAD_UP,
  BTN_DPAD_LEFT,
  BTN_TL2,
  BTN_NORTH,
  BTN_EAST,
  BTN_WEST,
  BTN_SOUTH,
  BTN_TR2,
};

static void setup_abs(int fd, unsigned chan, int min, int max) {
  struct uinput_abs_setup s = {
    .code = chan,
    .absinfo = { .minimum = min,  .maximum = max },
  };
  ioctl(fd, UI_SET_ABSBIT, chan);
  ioctl(fd, UI_ABS_SETUP, &s);
}
int uinput_init() {
  struct uinput_setup usetup;
  int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (fd < 0) return -1;
  ioctl(fd, UI_SET_EVBIT, EV_KEY);
  for (int i = 0; i < 16; i++) {
    classic_button_map[i] && ioctl(fd, UI_SET_KEYBIT, classic_button_map[i]);
  }
  /* Analog input not supported yet, but adding the basic axes is required
     for some software to recognize the controller as a gamepad. */
  ioctl(fd, UI_SET_EVBIT, EV_ABS);
  setup_abs(fd, ABS_X,  -127, 127);
  setup_abs(fd, ABS_Y,  -127, 127);
  setup_abs(fd, ABS_Z,  -127, 127);
  setup_abs(fd, ABS_RZ,  -127, 127);
  memset(&usetup, 0, sizeof(usetup));
  usetup.id.bustype = BUS_USB;
  usetup.id.vendor = 0x1209; // Generic
  usetup.id.product = 0x7501;
  strcpy(usetup.name, UINPUT_DEV_NAME);
  ioctl(fd, UI_DEV_SETUP, &usetup);
  ioctl(fd, UI_DEV_CREATE);
  return fd;
}
void uinput_emit(int fd, int type, int code, int val) {
  struct input_event ie;
  ie.type = type;
  ie.code = code;
  ie.value = val;
  ie.time.tv_sec = 0;
  ie.time.tv_usec = 0;
  write(fd, &ie, sizeof(ie));
}

void emit_events(int fd, int r, int r_prev) {
  int syn = 0;
  for (int i = 0; i < 16; i++) {
    int btn = classic_button_map[i];
    if (btn && GET(r, i) != GET(r_prev, i)) {
      uinput_emit(fd, EV_KEY, btn, GET(r, i));
      syn = 1;
    }
  }
  if (syn) {
    uinput_emit(fd, EV_SYN, SYN_REPORT, 0);
  }
}

#define RECONNECT_DELAY_US 500000
#define RETRY_DELAY_US 1000
#define RETRIES_MAX 5
#define I2C_DELAY_US 1

int read_bytes(int file, int offset, int c, char *to) {
  int res = i2c_smbus_write_byte(file, offset);
  if (res < 0) return res;
  usleep(I2C_DELAY_US);
  for (int i = 0; i < c; i++) {
    res = i2c_smbus_read_byte(file);
    if (res < 0) return res;
    to[i] = res;
    usleep(I2C_DELAY_US);
  }
  return c;
}
int initialize(int file) {
  int res = i2c_smbus_write_byte_data(file, 0xf0, 0x55);
  if (res < 0) return res;
  usleep(I2C_DELAY_US);
  res = i2c_smbus_write_byte_data(file, 0xfb, 0x00);
  if (res < 0) return res;
  usleep(I2C_DELAY_US);
  return 0;
}

int main(int argc, char **argv) {
  int adapter_nr = 1;
  int hz = 60;
  char filename[20];
  int opt;
  while ((opt = getopt(argc, argv, "y:f:")) != -1) {
    switch (opt) {
    case 'y':
        adapter_nr = atoi(optarg);
        break;
    case 'f':
        hz = atoi(optarg);
        if (hz < 1) hz = 1;
        if (hz > 1000) hz = 1000;
        break;
    default:
        fprintf(stderr, "Usage: %s [-f <scan freq=60>] [-y <bus index=1>]\n", argv[0]);
        exit(EXIT_FAILURE);
    }
  }

  snprintf(filename, sizeof(filename) - 1, "/dev/i2c-%d", adapter_nr);
  int file = open(filename, O_RDWR);
  int uinput_fd = uinput_init();
  if (uinput_fd < 0) {
    perror("uinput init");
    exit(1);
  }
  if (file < 0) {
    perror("open");
    exit(1);
  }
  if (ioctl(file, I2C_SLAVE, 0x52) < 0) {
    perror("ioctl");
    exit(1);
  }
  unsigned short r_prev = 0;
  int connected = 0;
  int hearbeat = 0;
  for(;;) {
    if (connected) {
      unsigned short r;
      int res = read_bytes(file, 0x04, 2, (void*)&r);
      for(int retries = 0; res < 0 && retries < RETRIES_MAX; retries++) {
        usleep(RETRY_DELAY_US);
        res = read_bytes(file, 0x04, 2, (void*)&r);
      }
      if (res < 0) {
        connected = 0;
        r_prev = 0;
        perror("read");
        continue;
      }
      if (r != r_prev) {
        printf("rd %04X\r", r ^ 0xffff);
        fflush(stdout);
      }
      emit_events(uinput_fd, r, r_prev);
      r_prev = r;
      if (++hearbeat % (hz * 2) == 0) {
        unsigned char id;
        read_bytes(file, 0xfc, 1, &id);
        if (id == 0xff /* != 0xa4 */) {
          fprintf(stderr, "controller setup lost: %02x\n", id);
          connected = 0;
          r_prev = 0;
          continue;
        }
      }
      usleep(1000000 / hz);
    } else {
      unsigned char id[6];
      if (initialize(file) < 0 || read_bytes(file, 0xfa, 6, id) < 0) {
        perror("init");
        usleep(RECONNECT_DELAY_US);
        continue;
      }
      printf("Detected device:");
      for(int i = 0; i < 6; i++) {
        printf(" %02X", id[i]);
      }
      printf("\n");
      if (id[2] != 0xa4 || id[3] != 0x20) {
        fprintf(stderr, "unknown device id %02x%02x\n", (int)id[2], (int)id[3]);
        usleep(RECONNECT_DELAY_US);
        continue;
      }
      if (id[5] != 0x01) {
        fprintf(stderr, "wrong device type %02x\n", (int)id[5]);
        usleep(RECONNECT_DELAY_US);
        continue;
      }
      connected = 1;
    }
  }
  return 0;
}
