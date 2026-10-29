#include <switch.h>
#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "jni_shim.h"
extern unsigned unity_audio_stop;
/* Mirror FMODAudioDevice.run: query the mixer format, then deliver each
 * complete PCM block to Horizon through SDL's queued audio backend. */
void *unity_audio_entry(void *env) {
    SDL_AudioDeviceID output=0;
    unsigned blocks=0,nonzero=0;
    int opened_rate=0,opened_block=0,opened_channels=0;
    while(!__atomic_load_n(&unity_audio_stop,__ATOMIC_ACQUIRE)) {
        int (*info)(void *,void *,int)=jni_find_native("fmodGetInfo");
        int (*process)(void *,void *,void *)=jni_find_native("fmodProcess");
        void *device=jni_fmod_device();
        if(!info || !process || !jni_fmod_should_run() || info(env,device,3)!=1) {
            if(output) { SDL_CloseAudioDevice(output); output=0; }
            svcSleepThread(20000000); continue;
        }
        int rate=info(env,device,0),block=info(env,device,1),channels=info(env,device,4);
        int64_t bytes=(int64_t)block*channels*2;
        if(rate<8000 || rate>192000 || block<16 || block>8192 || channels<1 || channels>2 || bytes>32768) {
            svcSleepThread(100000000); continue;
        }
        if(output && (rate!=opened_rate || block!=opened_block || channels!=opened_channels)) {
            SDL_CloseAudioDevice(output); output=0;
        }
        if(!output) {
            if(SDL_InitSubSystem(SDL_INIT_AUDIO)) { svcSleepThread(250000000); continue; }
            SDL_AudioSpec want={0}; want.freq=rate; want.format=AUDIO_S16SYS;
            want.channels=(Uint8)channels; want.samples=(Uint16)(block<128 ? 128 : block>4096 ? 4096 : block);
            output=SDL_OpenAudioDevice(NULL,0,&want,NULL,0);
            extern int debugPrintf(const char *,...);
            debugPrintf("GAME_AUDIO_OPEN device=%u rate=%d channels=%d block=%d error=%s\n",output,rate,channels,block,output ? "none" : SDL_GetError());
            if(!output) { svcSleepThread(250000000); continue; }
            opened_rate=rate; opened_block=block; opened_channels=channels;
            SDL_PauseAudioDevice(output,0);
        }
        if(SDL_GetQueuedAudioSize(output)>=(Uint32)bytes*3) { svcSleepThread(2000000); continue; }
        jni_fmod_set_pcm_size((int)bytes);
        memset(jni_fmod_pcm(),0,(size_t)bytes);
        int result=process(env,device,jni_fmod_bytebuffer());
        if(result || SDL_QueueAudio(output,jni_fmod_pcm(),(Uint32)bytes)) {
            SDL_CloseAudioDevice(output); output=0; svcSleepThread(20000000); continue;
        }
        const int16_t *pcm=jni_fmod_pcm();
        for(int i=0;i<bytes/2;++i) if(pcm[i]) { ++nonzero; break; }
        if(++blocks==1 || blocks%1024==0) {
            extern int debugPrintf(const char *,...);
            debugPrintf("GAME_AUDIO_PCM blocks=%u nonzero_blocks=%u queued_bytes=%u audible_confirmation=REQUIRED\n",blocks,nonzero,SDL_GetQueuedAudioSize(output));
        }
    }
    if(output) SDL_CloseAudioDevice(output);
    return NULL;
}
