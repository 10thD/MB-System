#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <termios.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <ctype.h>

#if defined(__CYGWIN__)
#define WIN_DECLSPEC __declspec(dllimport)
#else
#define WIN_DECLSPEC
#endif

#undef EMS_WITH_XONXOFF
#define XON 0x11
#define XOFF 0x13

typedef struct app_cfg_s {
    int verbose;
    char *ser_device;
    unsigned int ser_baud;
    unsigned int ser_delay_us;
    char *ifile;
    int flow;
}app_cfg_t;

static void s_show_help();
static app_cfg_t *s_cfg_new();
static void s_cfg_destroy(app_cfg_t **pself);
static void s_cfg_show(app_cfg_t *self);
static void s_parse_args(int argc, char **argv, app_cfg_t *cfg);

static bool g_interrupt=false;

static void s_parse_args(int argc, char **argv, app_cfg_t *cfg)
{
    extern char WIN_DECLSPEC *optarg;
    int option_index;
    int c;
    bool help=false;
    bool version=false;

    static struct option options[] = {
        {"verbose", required_argument, NULL, 0},
        {"help", no_argument, NULL, 0},
        {"device", required_argument, NULL, 0},
        {"baud", required_argument, NULL, 0},
        {"delay", required_argument, NULL, 0},
        {"flow", required_argument, NULL, 0},
        {NULL, 0, NULL, 0}};

    // process argument list
    while ((c = getopt_long(argc, argv, "", options, &option_index)) != -1){
        switch (c) {
                // long options all return c=0
            case 0:
                // verbose
                if (strcmp("verbose", options[option_index].name) == 0) {
                    sscanf(optarg,"%d",&cfg->verbose);
                }
                // help
                else if (strcmp("help", options[option_index].name) == 0) {
                    help = true;
                }
                // port
                else if (strcmp("device", options[option_index].name) == 0) {
                    free(cfg->ser_device);
                    cfg->ser_device = strdup(optarg);
                }
                // baud
                else if (strcmp("baud", options[option_index].name) == 0) {
                    sscanf(optarg,"%u",&cfg->ser_baud);
                }
                // delay
                else if (strcmp("delay", options[option_index].name) == 0) {
                    sscanf(optarg,"%u",&cfg->ser_delay_us);
                }
                // flow
                else if (strcmp("flow", options[option_index].name) == 0) {
                    int flow=0;
                    sscanf(optarg,"%c", (char *)&flow);
                    if(toupper(flow) == 'R')
                        cfg->flow = 'R';
                    else if(toupper(flow) == 'X')
                        cfg->flow = 'X';
                    else if(toupper(flow) == 'N')
                        cfg->flow = 'N';
                }
            default:
                break;
        }
    }

    for (int i=optind; i<argc; i++) {
        free(cfg->ifile);
        cfg->ifile = strdup(argv[i]);
//        mlist_add(cfg->file_paths,strdup(argv[i]));
    }

    return;
}

static void s_show_help()
{
    char help_message[] = "\n publish em710 UDP capture data to serial port (emulate M3 serial output)\n";
    char usage_message[] = "\n emserpub [options] file [file...]\n"
    "\n Options:\n"
    "  --verbose=n    : verbose output level\n"
    "  --help         : show this help message\n"
    "  --device=s     : serial port device\n"
    "  --baud=n       : serial comms rate\n"
    "  --delay=n      : interchacter delay (usec)\n"
    "\n";
    printf("%s",help_message);
    printf("%s",usage_message);
}

static app_cfg_t *s_cfg_new()
{
    app_cfg_t *new_cfg = (app_cfg_t *)malloc(sizeof(app_cfg_t));
    if ( new_cfg != NULL) {

        new_cfg->verbose = 0;
        new_cfg->ser_device = strdup("/dev/ttyUSB0");
        new_cfg->ser_baud = 115200;
        new_cfg->ser_delay_us = 0;
        new_cfg->ifile = NULL;
        new_cfg->flow = 'R';
    }
    return new_cfg;
}

static void s_cfg_destroy(app_cfg_t **pself)
{
    if(pself != NULL) {
        app_cfg_t *self = *pself;
        if (self != NULL) {

            free(self->ser_device);
            free(self->ifile);
            free(self);
        }
        *pself = NULL;
    }
}

static void s_cfg_show(app_cfg_t *self)
{
    fprintf(stderr,"\n");
    fprintf(stderr,"device    %s\n", self->ser_device);
    fprintf(stderr,"baud      %u\n", self->ser_baud);
    fprintf(stderr,"flow      %c\n", self->flow);
    fprintf(stderr,"delay_us  %u\n", self->ser_delay_us);
    fprintf(stderr,"ifile     %s\n", self->ifile);
    fprintf(stderr,"verbose   %d\n", self->verbose);
    fprintf(stderr,"\n");
}

static void s_termination_handler (int signum)
{
    switch (signum) {
        case SIGINT:
        case SIGHUP:
        case SIGTERM:
            g_interrupt=true;
            break;
        default:
//            MX_ERROR_MSG("not handled[%d]\n",signum);
            break;
    }
}

int s_set_rts(int fd, bool state)
{
    int errors = 0;

    int modstat = 0;

    if(ioctl(fd, TIOCMGET, &modstat) != 0){
        fprintf(stderr, "ERR TIOCMGET- %d/%s\n", errno, strerror(errno));
        errors++;
    }

    if(errors == 0){

        // assert RTS (active low)
        if(state)
            modstat |= TIOCM_RTS;
        else
            modstat &= ~TIOCM_RTS;

        if(ioctl(fd, TIOCMSET, &modstat) != 0) {
            fprintf(stderr, "ERR TIOCMSET- %d/%s\n", errno, strerror(errno));
            errors++;
        }
    }
    //    fprintf(stderr, "%s: fd %d state %c errors %d\n", __func__, fd, (start_flow ? 'Y' : 'N'), errors);

    return (errors == 0 ? 0 : -1);
}

int s_set_cts(int fd, bool state)
{
    int errors = 0;

    int modstat = 0;

    if(ioctl(fd, TIOCMGET, &modstat) != 0){
        fprintf(stderr, "ERR TIOCMGET- %d/%s\n", errno, strerror(errno));
        errors++;
    }

    if(errors == 0){

        // assert RTS (active low)
        if(state)
            modstat |= TIOCM_CTS;
        else
            modstat &= ~TIOCM_CTS;

        if(ioctl(fd, TIOCMSET, &modstat) != 0) {
            fprintf(stderr, "ERR TIOCMSET- %d/%s\n", errno, strerror(errno));
            errors++;
        }
    }
    //    fprintf(stderr, "%s: fd %d state %c errors %d\n", __func__, fd, (start_flow ? 'Y' : 'N'), errors);

    return (errors == 0 ? 0 : -1);
}

void config_ser(int fd, app_cfg_t *cfg)
{
    struct termios tty;
    if(tcgetattr(fd, &tty) != 0) {
        fprintf(stderr, "Error %i from tcgetattr: %s\n", errno, strerror(errno));
    }
    cfmakeraw(&tty);

    tty.c_cflag &= ~(CSIZE|PARENB); // Clear parity bit
    tty.c_cflag &= ~CSTOPB; // Clear stop field (one stop bit)
    tty.c_cflag |= CS8;     // 8 bits per byte
    if(cfg->flow == 'R'){
        tty.c_cflag |= CRTSCTS; // Disable RTS/CTS hardware flow control
    }

#if 0
    tty.c_cflag |= CREAD; // Turn on READ & ignore ctrl lines
    tty.c_cflag |= CLOCAL; // Turn on READ & ignore ctrl lines
    tty.c_lflag &= ~ICANON;
    tty.c_lflag &= ~ECHO; // Disable echo
    tty.c_lflag &= ~ECHOE; // Disable erasure
    tty.c_lflag &= ~ECHONL; // Disable new-line echo
    tty.c_lflag &= ~ISIG; // Disable interpretation of INTR, QUIT and SUSP
    tty.c_lflag &= ~IEXTEN; // Disable implementation-defined input processing
    //    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // Enable s/w flow ctrl
    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL); // Disable any special handling of received bytes
    tty.c_oflag &= ~OPOST; // Prevent special interpretation of output bytes (e.g. newline chars)
    tty.c_oflag &= ~ONLCR; // Prevent conversion of newline to carriage return/line feed
    // tty.c_oflag &= ~OXTABS; // Prevent conversion of tabs to spaces (NOT PRESENT IN LINUX)
    // tty.c_oflag &= ~ONOEOT; // Prevent removal of C-d chars (0x004) in output (NOT PRESENT IN LINUX)
    tty.c_cc[VTIME] = 0;    // Wait for up to 1s (10 deciseconds), returning as soon as any data is received.
    tty.c_cc[VMIN] = 0;

    if(cfg->flow == 'X'){
        tty.c_iflag &= (IXON); // Enable s/w flow ctrl
        tty.c_iflag &= ~(IXOFF | IXANY); // Enable s/w flow ctrl
        tty.c_cc[VSTOP] = XOFF;
        tty.c_cc[VSTART] = XON;
    }
#endif


    // Set in/out baud rate
    switch(cfg->ser_baud){
        case 1200:
            cfsetispeed(&tty, B1200);
            cfsetospeed(&tty, B1200);
            break;
        case 1800:
            cfsetispeed(&tty, B1800);
            cfsetospeed(&tty, B1800);
            break;
        case 2400:
            cfsetispeed(&tty, B2400);
            cfsetospeed(&tty, B2400);
            break;
        case 4800:
            cfsetispeed(&tty, B9600);
            cfsetospeed(&tty, B9600);
            break;
        case 9600:
            cfsetispeed(&tty, B9600);
            cfsetospeed(&tty, B9600);
            break;
        case 19200:
            cfsetispeed(&tty, B19200);
            cfsetospeed(&tty, B19200);
            break;
        case 38400:
            cfsetispeed(&tty, B38400);
            cfsetospeed(&tty, B38400);
            break;
        case 57600:
            cfsetispeed(&tty, B57600);
            cfsetospeed(&tty, B57600);
            break;
        case 76800:
            cfsetispeed(&tty, B76800);
            cfsetospeed(&tty, B76800);
            break;
        case 115200:
            cfsetispeed(&tty, B115200);
            cfsetospeed(&tty, B115200);
            break;
        default:
            fprintf(stderr, "ERR - invalid ser_baud %u\n", cfg->ser_baud);
            break;
    };
    return;
}

int main(int argc, char **argv)
{
    struct sigaction saStruct;
    sigemptyset(&saStruct.sa_mask);
    saStruct.sa_flags = 0;
    saStruct.sa_handler = s_termination_handler;
    sigaction(SIGINT, &saStruct, NULL);

    app_cfg_t *cfg = s_cfg_new();
    s_parse_args(argc, argv, cfg);
    s_cfg_show(cfg);

    // open output port
    int fd = open(cfg->ser_device, O_RDWR|O_NOCTTY);

    if(fd < 0){
        fprintf(stderr, "could not open %s %d/%s\n", cfg->ser_device, errno, strerror(errno));
        s_cfg_destroy(&cfg);
        return -1;
    }

    config_ser(fd, cfg);

    // open input file
    FILE *fp = fopen(cfg->ifile, "rb");

    uint64_t obytes = 0;

    if(fp != NULL){

        if(cfg->verbose > 0 ){
            fprintf(stderr, "%s input file %s open fp %p\n", __func__, cfg->ifile, fp);
            fprintf(stderr, "%s output device %s connected fd %d %u bps\n", __func__, cfg->ser_device, fd, cfg->ser_baud);
        }

        fseek(fp, 0, SEEK_END);
        off_t fend = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        if(cfg->verbose > 0 ){
            fprintf(stderr, "ftell %zd fend %lld\n", ftell(fp), fend);
        }

        const size_t IBUF_LEN = 4096;
        unsigned char ibuf[IBUF_LEN]={0};
        bool do_quit=false;
        bool do_tx=true;
        int64_t burst_count = 0;

        while(!do_quit && !g_interrupt) {

            memset(ibuf, 0 , IBUF_LEN);

            // quit if end of input
            if(ftell(fp) >= fend)
                break;

            if(cfg->flow == 'R'){
                // set RTS
                s_set_rts(fd, true);

                // wait for CTS
                while( !g_interrupt){
                    int modstat=0;
                    if(ioctl(fd, TIOCMGET, &modstat) != 0)
                        fprintf(stderr, "ERR TIOCMGET- %d/%s\n", errno, strerror(errno));

                    if((modstat&TIOCM_CTS) != 0){
                        fprintf(stderr, "\nENABLE TX\n");
                        do_tx = true;
                        burst_count = 0;
                        break;
                    }
                    usleep(10000);
                }
            }


            while(do_tx && !g_interrupt){

                if(cfg->flow == 'R'){
                    // check CTS, stop sending if asserted
                    int modstat=0;
                    if(ioctl(fd, TIOCMGET, &modstat) != 0)
                        fprintf(stderr, "ERR TIOCMGET- %d/%s\n", errno, strerror(errno));

                    if((modstat&TIOCM_CTS) == 0){
                        fprintf(stderr, "\nDISABLE TX (%lld bytes)\n", burst_count);
                        do_tx = false;
                        break;
                    }
                }

                if(do_tx){
                    // read byte(s) from input file
                    size_t rbytes = fread(&ibuf, 1, IBUF_LEN, fp);

                    if( rbytes > 0) {

                        burst_count += rbytes;

                        // write byte(s) to output (should block until sent)
                        ssize_t wb = write(fd, ibuf, rbytes);
                        tcdrain(fd);

                        if(wb <= 0) {
                            fprintf(stderr, "\nERR - write returned %zd %d/%s\n", wb, errno, strerror(errno));
                        } else if(wb < rbytes){
                            fprintf(stderr, "\nWARN - write returned %zd/%zd\n", wb, rbytes);
                        }

                        // display bytes
                        if(cfg->verbose >= 2 ){
                            for(int i = 0; i < rbytes; i++){
                                if((obytes % 16) == 0)
                                    fprintf(stderr, "\n%08llx: ", obytes);
                                fprintf(stderr, "%02X ", ibuf[i]);
                                obytes++;
                            }
                        }

                        if(cfg->ser_delay_us > 0)
                            usleep(cfg->ser_delay_us);

                    } else if (rbytes < 0) {
                        // read error (EOF?)
                        do_quit = true;
                    }
                }
            }

        }
    } else {
        fprintf(stderr, "ERR - file %s %d/%s\n", cfg->ifile, errno, strerror(errno));
    }

    if(cfg->verbose > 0 ){
        fprintf(stderr, "\nwrote %llu/%08llX bytes\n", obytes, obytes);
    }

    close(fd);
    fclose(fp);

    free(cfg->ser_device);
    free(cfg->ifile);
    return 0;
}
