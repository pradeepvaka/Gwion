#include <stdlib.h>
#include <string.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include "vm.h"
#include "err_msg.h"
#include "driver.h"

ANN m_bool init_bbq(VM* vm, DriverInfo* di, Driver* d) {
  di->func(d);
  if(d->ini(vm, di) < 0)
    return -1; // LCOV_EXCL_LINE
  sp_createn(&vm->sp, di->out);
  vm->sp->out   = xrealloc(vm->sp->out, di->out * SZ_FLOAT);
  vm->in   = xcalloc(di->in, SZ_FLOAT);
  vm->n_in = di->in;
  vm->sp->sr = di->sr;
  sp_srand(vm->sp, time(NULL));
  return 1;
}

void select_driver(DriverInfo* di, const m_str d) {
  if(!strcmp("dummy", d))
    di->func = dummy_driver;
  else if(!strcmp("silent", d))
    di->func = silent_driver;

#ifdef HAVE_SNDFILE
  else if(!strcmp("sndfile", d)) {
    di->func = sndfile_driver;
    di->card = "/tmp/gwion";
  }
#endif

#ifdef HAVE_SPA
  else if(!strcmp("spa", d)) {
    di->func = spa_driver;
    di->card = "/tmp/gwion";
  }
#endif

#ifdef HAVE_ALSA
  else if(!strcmp("alsa", d)) {
    di->func = alsa_driver;
    di->format = SND_PCM_FORMAT_FLOAT64;
    di->card = "default";
  }
#endif

#ifdef HAVE_JACK
  else if(!strcmp("jack", d)) {
    di->func = jack_driver;
//		di->card = "default";
  }
#endif

#ifdef HAVE_SOUNDIO
  else if(!strcmp("soundio", d)) {
    di->func = sio_driver;
    di->backend = SoundIoBackendNone;
    di->format = SoundIoFormatFloat32NE;
//		di->card = "default";
  }
#endif

#ifdef HAVE_PORTAUDIO
  else if(!strcmp("portaudio", d)) {
    di->func = pa_driver;
    di->format = paFloat32;
    di->card = "default";
  }
#endif
#ifdef HAVE_PULSE
  else if(!strcmp("pulse", d)) {
    di->func = pulse_driver;
//    di->format = paFloat32;
//    di->card = "default";
  }
#endif
#ifdef HAVE_PLOT
  else if(!strcmp("plot", d))
    di->func = plot_driver;
#endif
  else
    gw_err("invalid driver specified. using default.\n");
}

void select_backend(DriverInfo* di __attribute__((unused)), const m_str d __attribute__((unused))) {
#ifdef HAVE_SOUNDIO
  if(!strcmp("dummy", d))
    di->backend = SoundIoBackendDummy;
  else if(!strcmp("alsa", d))
    di->backend = SoundIoBackendAlsa;
  else if(!strcmp("jack", d))
    di->backend = SoundIoBackendJack;
  else if(!strcmp("pulse", d))
    di->backend = SoundIoBackendPulseAudio;
  else if(!strcmp("core", d))
    di->backend = SoundIoBackendCoreAudio;
  else if(!strcmp("wasapi", d))
    di->backend = SoundIoBackendWasapi;
#endif
}

void select_format(DriverInfo* di __attribute__((unused)), const m_str d __attribute__((unused))) {
#ifdef HAVE_ALSA
  if(di->func == alsa_driver) {
    if(!strcmp("S8", d))
      di->format = SND_PCM_FORMAT_S8;
    if(!strcmp("S16", d))
      di->format = SND_PCM_FORMAT_S16;
    else if(!strcmp("U16", d))
      di->format = SND_PCM_FORMAT_U16;
    else if(!strcmp("S24", d))
      di->format = SND_PCM_FORMAT_S24;
    else if(!strcmp("U24", d))
      di->format = SND_PCM_FORMAT_U24;
    else if(!strcmp("S32", d))
      di->format = SND_PCM_FORMAT_S32;
    else if(!strcmp("U32", d))
      di->format = SND_PCM_FORMAT_U32;
    else if(!strcmp("F32", d))
      di->format = SND_PCM_FORMAT_FLOAT;
    else if(!strcmp("F64", d))
      di->format = SND_PCM_FORMAT_FLOAT64;
  }
#endif

#ifdef HAVE_SOUNDIO
  if(di->func == sio_driver) {
    if(!strcmp("S16", d))
      di->format = SoundIoFormatS16NE;
    else if(!strcmp("U16", d))
      di->format = SoundIoFormatU16NE;
    else if(!strcmp("S24", d))
      di->format = SoundIoFormatS24NE;
    else if(!strcmp("U24", d))
      di->format = SoundIoFormatU24NE;
    else if(!strcmp("S32", d))
      di->format = SoundIoFormatS32NE;
    else if(!strcmp("U32", d))
      di->format = SoundIoFormatU32NE;
    else if(!strcmp("F32", d))
      di->format = SoundIoFormatFloat32NE;
    else if(!strcmp("F64", d))
      di->format = SoundIoFormatFloat64NE;
  }
#endif

#ifdef HAVE_PORTAUDIO
  if(di->func == pa_driver) {
    if(!strcmp("S32", d) || !strcmp("U32", d))
      di->format = paInt32;
    if(!strcmp("S24", d) || !strcmp("U24", d))
      di->format = paInt24;
    if(!strcmp("S16", d) || !strcmp("U16", d))
      di->format = paInt16;
    else if(!strcmp("S8", d))
      di->format = paInt8;
    else if(!strcmp("U8", d))
      di->format = paUInt8;
// paCustomFormat
  }
#endif

}
