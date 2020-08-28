#include <linux/i2c-dev.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>

#define UINPUT_DEV_NAME "Nintendo I²C Controller"

int hq = 0;
int analog = 0;
int debug = 0;

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
int classic_axis_map[6] = {
  ABS_X, ABS_RX,
  ABS_Y, ABS_RY,
  ABS_Z, ABS_RZ,
};
uint8_t invert_axis[6] = { 0, 0, 1, 1, 0, 0 };
static void setup_abs(int fd, int chan, int min, int max) {
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
  /* Adding the basic axes is required for some software to recognize the controller as a gamepad. */
  ioctl(fd, UI_SET_EVBIT, EV_ABS);
  setup_abs(fd, ABS_X,  -127, 127);
  setup_abs(fd, ABS_Y,  -127, 127);
  setup_abs(fd, ABS_RX,  -127, 127);
  setup_abs(fd, ABS_RY,  -127, 127);
  if (analog == 6) {
    setup_abs(fd, ABS_Z,  -127, 127);
    setup_abs(fd, ABS_RZ,  -127, 127);
  }
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

#define GET_BUTTON(r, i) ((r & (1 << i)) == 0)
void emit_events(int fd, uint16_t r, uint16_t r_prev, uint8_t *a, uint8_t *a_prev) {
  int syn = 0;
  if ((r & 0xFF) == 0 || (r & 0xFF00) == 0) {
    if (debug) {
      printf("ERR \r");
      fflush(stdout);
    }
    return;
  }
  for (int i = 0; i < 16; i++) {
    int btn = classic_button_map[i];
    if (btn && GET_BUTTON(r, i) != GET_BUTTON(r_prev, i)) {
      uinput_emit(fd, EV_KEY, btn, GET_BUTTON(r, i));
      syn = 1;
    }
  }
  for (int i = 0; i < analog; i++) if (a[i] != a_prev[i]) {
    uinput_emit(fd, EV_ABS, classic_axis_map[i], invert_axis[i] ? 128 - a[i] : a[i] - 128);
    syn = 1;
  }
  if (syn) {
    if (debug) {
      if (analog) printf("axes x:%4d y:%4d rx:%4d ry:%4d ", a[0] - 128, 128 - a[2], a[1] - 128, 128 - a[3]);
      if (analog == 6) printf("z:%02X rz:%02X ", a[4] - 128, a[5] - 128);
      printf("buttons: ");
      for (int i = 15; i > 0; i--) putchar(GET_BUTTON(r, i) ? " R+H-Lv>^<rXAYBl"[i] : ' ');
      putchar('\r');
      fflush(stdout);
    }
    uinput_emit(fd, EV_SYN, SYN_REPORT, 0);
  }
}
#define LX(a) ((a)->lx << 2)
#define LY(a) ((a)->ly << 2)
#define RX(a) (((a)->rx2 + ((a)->rx1 << 1) + ((a)->rx0 << 3)) << 3)
#define RY(a) ((a)->ry << 3)
#define LT(a) (((a)->lt1 + ((a)->lt0 << 3)) << 3)
#define RT(a) ((a)->rt << 3)
void to_hq(uint8_t from[4], uint8_t to[6]) {
  struct {
    uint8_t lx:6, rx0:2;
    uint8_t ly:6, rx1:2;
    uint8_t ry:5, lt0:2;
    uint8_t rx2:1, rt:5, lt1:3;
  } *a = (void*)from;
  to[0] = a->lx << 2;
  to[1] = (a->rx2 + (a->rx1 << 1) + (a->rx0 << 3)) << 3;
  to[2] = a->ly << 2;
  to[3] = a->ry << 3;
  to[4] = (a->lt1 + (a->lt0 << 3)) << 3;
  to[5] = a->rt << 3;
}
void emit_analog(int fd, char *pr, char *pr_prev) {
  unsigned char hq_axes[6], hq_axes_prev[6];
  to_hq(pr, hq_axes);
  to_hq(pr_prev, hq_axes_prev);
  emit_events(fd, *(uint16_t *)(pr + 4), *(uint16_t *)(pr_prev + 4), hq_axes, hq_axes_prev);
}
void emit_analog_hq(int fd, char *pr, char *pr_prev) {
  emit_events(fd, *(uint16_t *)(pr + 6), *(uint16_t *)(pr_prev + 6), pr, pr_prev);
}
void emit_digital(int fd, char *pr, char *pr_prev) {
  emit_events(fd, *(uint16_t *)pr, *(uint16_t *)pr_prev, 0, 0);
}

#define RECONNECT_DELAY_US 500000
#define RETRY_DELAY_US 1000
#define RETRIES_MAX 5
#define I2C_DELAY_US 10
#define I2C_READ_AT_ZERO_DELAY_US 120

int long_wait = 0;
int read_bytes(int file, int offset, int c, char *to) {
  int res = i2c_smbus_write_byte(file, offset);
  if (res < 0) return res;
  usleep(offset == 0 ? I2C_READ_AT_ZERO_DELAY_US : I2C_DELAY_US);
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
  res = i2c_smbus_write_byte_data(file, 0xfe, hq ? 0x03 : 0x01);
  if (res < 0) return res;
  usleep(I2C_DELAY_US);
  return 0;
}

int main(int argc, char **argv) {
  int adapter_nr = 1;
  int hz = 60;
  char filename[20];
  int opt;
  while ((opt = getopt(argc, argv, "y:f:ha::d")) != -1) {
    switch (opt) {
    case 'y':
        adapter_nr = atoi(optarg);
        break;
    case 'f':
        hz = atoi(optarg);
        if (hz < 1) hz = 1;
        if (hz > 1000) hz = 1000;
        break;
    case 'h':
        hq = 1;
        break;
    case 'a':
        analog = optarg ? atoi(optarg) : 4;
        if (analog != 0 && analog != 4 && analog != 6) analog = 0;
        break;
    case 'd':
        debug = 1;
        break;
    default:
        fprintf(stderr, "Usage: %s [-d] [-f <scan freq=60>] [-y <bus index=1>] [-h] [-a|-a6]\n", argv[0]);
        exit(EXIT_FAILURE);
    }
  }
  int read_len = analog ? hq ? 8 : 6 : 2;
  int read_addr = analog ? 0 : hq ? 6 : 4;
  void (*parse_func)(int fd, char *pr, char *pr_prev) = analog ? hq ? emit_analog_hq : emit_analog : emit_digital;

  snprintf(filename, sizeof(filename) - 1, "/dev/i2c-%d", adapter_nr);
  int file = open(filename, O_RDWR);
  int uinput_fd = uinput_init();
  if (uinput_fd < 0) {
    perror("uinput init");
    exit(EXIT_FAILURE);
  }
  if (file < 0) {
    perror("open i2c device");
    exit(EXIT_FAILURE);
  }
  if (ioctl(file, I2C_SLAVE, 0x52) < 0) {
    perror("ioctl");
    exit(EXIT_FAILURE);
  }
  uint64_t r_prev;
  int connected = 0;
  int hearbeat = 0;
  for(;;) {
    if (connected) {
      uint64_t r;
      int res = read_bytes(file, read_addr, read_len, (void *)&r);
      for(int retries = 0; res < 0 && retries < RETRIES_MAX; retries++) {
        usleep(RETRY_DELAY_US);
        res = read_bytes(file, read_addr, read_len, (void *)&r);
      }
      if (res < 0) {
        connected = 0;
        r_prev = 0;
        perror("read");
        continue;
      }
      parse_func(uinput_fd, (void*)&r, (void *)&r_prev);
      r_prev = r;
      if (++hearbeat % (hz * 2) == 0) {
        uint8_t id;
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
      uint8_t id[6];
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
  return EXIT_SUCCESS;
}
