#define _GNU_SOURCE
 
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

const char *gpio475="/sys/class/gpio/gpio475/value"; // the trigger
const char *gpio485="/sys/class/gpio/gpio485/value"; // the echo


static inline long cycles_us(void)
{
	struct timeval t;

	gettimeofday(&t);
	return t.tv_sec * 1000000 + t.tv_usec;
}

int trig(char *onoff)
{
	int b;
	int fd;

	fd = open(gpio475, O_WRONLY);
	if (fd < 0){
		perror("Failed to open gpio485 file");
		return -1;
	}
	
	b = write(fd, onoff, 2);
	if (b < 0){
		perror("write:");
		return -1;
	}
	close(fd);
	return 0;

}

long wait_echo(char c)
{
	long t = 0;
        int b;
        int fd;
        char buf[32];
wait:
        fd = open(gpio485, O_RDONLY);
        if (fd < 0){
                perror("Failed to open gpio475 file");
                return ;
        }

        b = read(fd, buf, sizeof(buf));
        if (b < 0){
                perror("read:");
                close(fd);
                goto wait;
        }
        close(fd);
        if (buf[0] != c) {
		t = cycles_us();
                goto wait;
        }
	return t;
}

int main(int argc,char *argv[])
{
	int i;
	int sleep_us;
	long s, e, dt_ms, tmp;
	float supersonic_speed_ms = 343;// centimeter/milli;	
	float distance;

	if (argc < 2) {
		printf("%s <wait time us>\n",argv[0]);
		return -1;
	}
	
	sleep_us = atoi(argv[1]);
	trig("1\n");
	// wait trigger
	usleep(sleep_us);
	trig("0\n");
	s =  cycles_us();
	tmp = wait_echo('0');
	if (tmp != 0)
		s = tmp;
	tmp  = wait_echo('1');
	if (tmp != 0)
		e = tmp;
	dt_ms = (e - s)/1000000;

	distance  = ((float)dt_ms * supersonic_speed_ms)/2;
	
	printf("distance %f cm dt=%ld (e-s)=%ld\n",
		distance, dt_ms, e-s);
}
