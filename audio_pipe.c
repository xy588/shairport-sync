/*
 * pipe output driver. This file is part of Shairport.
 * Copyright (c) James Laird 2013
 * All rights reserved.
 *
 * Modifications for audio synchronisation
 * and related work, copyright (c) Mike Brady 2014
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "audio.h"
#include "common.h"
#include <errno.h>
#include <fcntl.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int fd = -1;

char *pipename = NULL;
int warned = 0;

static void start(int sample_rate, int sample_format) {
  // this will leave fd as -1 if a reader hasn't been attached
  fd = open(pipename, O_WRONLY | O_NONBLOCK);
  if ((fd<-1) && (warned==0)) {
    warn("Error %d opening the pipe named \"%s\".",errno,pipename);
    warned = 1;
  }
}

static void play(short buf[], int samples) {
  // if the file is not open, try to open it.
  char errorstring[1024];
  if (fd == -1) {
    fd = open(pipename, O_WRONLY | O_NONBLOCK);
  }
  // if it's got a reader, write to it.
  if (fd > 0) {
    int rc = non_blocking_write(fd, buf, samples * 4);
    if ((rc<0) && (warned==0)) {
      strerror_r(errno,(char*)errorstring,1024);
      warn("Error %d writing to the pipe named \"%s\": \"%s\".",errno,pipename,errorstring);
      warned = 1;
    }
  } else if ((fd == -1) && (warned==0)) {
    strerror_r(errno,(char*)errorstring,1024);
    warn("Error %d opening the pipe named \"%s\": \"%s\".",errno,pipename,errorstring);
    warned = 1;
   }
}

static void stop(void) {
  // Don't close the pipe just because a play session has stopped.
  //  if (fd > 0)
  //    close(fd);
}

static int init(int argc, char **argv) {
  debug(1, "pipe init");
  const char *str;
  int value;
  double dvalue;

  config.audio_backend_buffer_desired_length = 1.0;
  config.audio_backend_latency_offset = 0;

  if (config.cfg != NULL) {
    /* Get the Output Pipename. */
    const char *str;
    if (config_lookup_string(config.cfg, "pipe.name", &str)) {
      pipename = (char *)str;
    }
    
    /* Get the desired length of the period of silence before the audio starts. */
    if (config_lookup_float(config.cfg, "pipe.audio_backend_silent_lead_in_time",
                            &dvalue)) {
      if ((dvalue < 0.05) || (dvalue > 4)) {
        die("Invalid pipe audio_backend_silent_lead_in_time \"%f\". It "
            "must be between 0.050 and 4.0 seconds. Omit setting to use the default value, which is approximately the latency specified by the source (typically 2 seconds). A value greater than the latency is ignored.",
            dvalue);
      } else {
        config.audio_backend_silent_lead_in_time = dvalue;
      }
    }

    if ((pipename) && (strcasecmp(pipename, "STDOUT") == 0))
      die("Can't use \"pipe\" backend for STDOUT. Use the \"stdout\" backend instead.");

    /* Get the desired buffer size setting. */
    if (config_lookup_int(config.cfg, "pipe.audio_backend_buffer_desired_length", &value)) {
      if ((value < 0) || (value > 66150)) {
        inform("The setting pipe.audio_backend_buffer_desired_length is deprecated. "
               "Use pipe.audio_backend_buffer_desired_length_in_seconds instead.");
        die("Invalid pipe audio backend buffer desired length \"%d\". It "
            "should be between 0 and "
            "66150, default is 6615",
            value);
      } else {
        inform("The setting pipe.audio_backend_buffer_desired_length is deprecated. "
               "Use pipe.audio_backend_buffer_desired_length_in_seconds instead.");
        config.audio_backend_buffer_desired_length = 1.0 * value / 44100;
      }
    }

    /* Get the desired buffer size setting. */
    if (config_lookup_float(config.cfg, "pipe.audio_backend_buffer_desired_length_in_seconds",
                            &dvalue)) {
      if ((dvalue < 0) || (dvalue > 1.5)) {
        die("Invalid pipe audio backend buffer desired time \"%f\". It "
            "should be between 0 and "
            "1.5, default is 0.15 seconds",
            dvalue);
      } else {
        config.audio_backend_buffer_desired_length = dvalue;
      }
    }

    /* Get the latency offset. */
    if (config_lookup_int(config.cfg, "pipe.audio_backend_latency_offset", &value)) {
      if ((value < -66150) || (value > 66150)) {
        inform("The setting pipe.audio_backend_latency_offset is deprecated. "
               "Use pipe.audio_backend_latency_offset_in_seconds instead.");
        die("Invalid pipe audio backend buffer latency offset \"%d\". It "
            "should be between -66150 and +66150, default is 0",
            value);
      } else {
        inform("The setting pipe.audio_backend_latency_offset is deprecated. "
               "Use pipe.audio_backend_latency_offset_in_seconds instead.");
        config.audio_backend_latency_offset = 1.0 * value / 44100;
      }
    }

    /* Get the latency offset. */
    if (config_lookup_float(config.cfg, "pipe.audio_backend_latency_offset_in_seconds", &dvalue)) {
      if ((dvalue < -1.0) || (dvalue > 1.5)) {
        die("Invalid pipe audio backend buffer latency offset time \"%f\". It "
            "should be between -1.0 and +1.5, default is 0 seconds",
            dvalue);
      } else {
        config.audio_backend_latency_offset = dvalue;
      }
    }
  }
  if ((pipename == NULL) && (argc != 1))
    die("bad or missing argument(s) to pipe");

  if (argc == 1)
    pipename = strdup(argv[0]);

  // here, create the pipe
  if (mkfifo(pipename, 0644) && errno != EEXIST)
    die("Could not create output pipe \"%s\"", pipename);

  debug(1, "Pipename is \"%s\"", pipename);

  return 0;
}

static void deinit(void) {
  if (fd > 0)
    close(fd);
}

static void help(void) { printf("    pipe takes 1 argument: the name of the FIFO to write to.\n"); }

audio_output audio_pipe = {.name = "pipe",
                           .help = &help,
                           .init = &init,
                           .deinit = &deinit,
                           .start = &start,
                           .stop = &stop,
                           .flush = NULL,
                           .delay = NULL,
                           .play = &play,
                           .volume = NULL,
                           .parameters = NULL,
                           .mute = NULL};
